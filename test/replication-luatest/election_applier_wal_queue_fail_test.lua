local t = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')

local wait_timeout = 120
local g = t.group()

g.before_all(function(g)
    t.tarantool.skip_if_not_debug()
    g.cluster = cluster:new({})
    local box_cfg = {
        replication_timeout = 0.1,
        replication_synchro_quorum = 1,
        replication_synchro_timeout = 1000,
        election_mode = 'manual',
        replication = {
            server.build_listen_uri('server1', g.cluster.id),
            server.build_listen_uri('server2', g.cluster.id),
        },
    }
    g.server1 = g.cluster:build_and_add_server({
        alias = 'server1', box_cfg = box_cfg
    })
    box_cfg.election_mode = 'voter'
    box_cfg.wal_queue_max_size = 1
    g.server2 = g.cluster:build_and_add_server({
        alias = 'server2', box_cfg = box_cfg
    })
    g.cluster:start()
    g.server1:exec(function()
        local s = box.schema.create_space('test_local', {is_local = true})
        s:create_index('pk')
    end)
    g.server2:wait_for_vclock_of(g.server1)
end)
g.after_all(function(g)
    g.cluster:drop()
end)

--
-- There was a bug that if a synchronous request in applier was rolled back
-- while waiting in the journal's volatile queue, then it would try to be rolled
-- back second time and in case of PROMOTE/DEMOTE it would crash in debug + have
-- undefined behaviour in release.
--
-- But ideally it must just break the replication in a recoverable way.
--
g.test_case = function(g)
    --
    -- The next incoming PROMOTE will get blocked before starting a WAL write.
    --
    g.server2:exec(function()
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY_COUNTDOWN', 0)
    end)
    --
    -- Server 1 sends a PROMOTE.
    --
    local term = g.server1:exec(function()
        box.ctl.demote()
        box.ctl.promote()
        return box.info.synchro.queue.term
    end)
    g.server2:exec(function(timeout, term)
        local fiber = require('fiber')
        --
        -- The PROMOTE is received.
        --
        t.helpers.retrying({timeout = timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_TXN_LIMBO_BEGIN_DELAY'))
        end)
        --
        -- But before it a transaction manages to squeeze in and it takes the
        -- whole WAL queue, forcing the PROMOTE journal entry to wait in the
        -- volatile state.
        --
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            box.space.test_local:replace{1}
        end)
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY', false)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert(box.info.synchro.queue.busy)
        end)
        --
        -- And the transaction fails to be written to WAL. Which causes
        -- cascading rollback of the PROMOTE entry.
        --
        box.error.injection.set('ERRINJ_WAL_ROTATE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok, err = f:join(timeout)
        t.assert(err)
        t.assert_not(ok)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_not(box.info.synchro.queue.busy)
        end)
        box.error.injection.set('ERRINJ_WAL_ROTATE', false)
        t.assert_lt(box.info.synchro.queue.term, term)
        --
        -- Replication can be restarted and fixed easy.
        --
        local repl = box.cfg.replication
        box.cfg{replication = {}}
        box.cfg{replication = repl}
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.synchro.queue.term, term)
        end)
    end, {wait_timeout, term})
end
