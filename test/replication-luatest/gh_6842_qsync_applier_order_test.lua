local luatest = require('luatest')
local server = require('test.luatest_helpers.server')
local cluster = require('test.luatest_helpers.cluster')

local wait_timeout = 120

local g = luatest.group('gh-6842')

g.before_all(function(g)
    g.cluster = cluster:new({})
    local box_cfg = {
        replication_timeout = 0.1,
        replication_synchro_quorum = 2,
        replication_synchro_timeout = 1000,
        replication = {
            server.build_instance_uri('server1'),
            server.build_instance_uri('server2'),
        },
    }
    g.server1 = g.cluster:build_and_add_server({
        alias = 'server1', box_cfg = box_cfg
    })
    -- For stability. To guarantee server1 is first, server2 is second.
    box_cfg.read_only = true
    g.server2 = g.cluster:build_and_add_server({
        alias = 'server2', box_cfg = box_cfg
    })
    g.cluster:start()

    g.server2:exec(function()
        box.cfg{
            read_only = false
        }
    end)
end)

g.after_all(function(g)
    g.cluster:drop()
    g.server1 = nil
    g.server2 = nil
end)

g.after_each(function(g)
    -- Restore cluster state like it was on start.
    g.server1:exec(function()
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 1000,
        }
        box.ctl.demote()
    end)
    g.server2:exec(function()
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 1000,
        }
        box.ctl.demote()
        if box.space.test then
            box.space.test:drop()
        end
    end)
    g.server1:wait_vclock_of(g.server2)
    g.server2:wait_vclock_of(g.server1)
end)

-- Allow WAL writes one by one until the synchro queue becomes busy. The
-- function is needed, because during box.ctl.promote() it is not known for sure
-- which WAL write is PROMOTE - first, second, third? Even if known, it might
-- change in the future.
local function play_wal_until_synchro_queue_is_busy(server)
    luatest.helpers.retrying({timeout = wait_timeout}, server.exec, server,
                             function()
        if not box.error.injection.get('ERRINJ_WAL_DELAY') then
            error('WAL did not reach the delay yet')
        end
        if box.info.synchro.queue.busy then
            return
        end
        -- Allow 1 more WAL write.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        error('Not busy yet')
    end)
end

--
-- Wait until the server sees synchro queue owner as the given ID.
--
local function wait_synchro_owner(server, owner_id)
    luatest.helpers.retrying({timeout = wait_timeout}, server.exec, server,
                             function(id)
        if box.info.synchro.queue.owner ~= id then
            error('Waiting for queue transition')
        end
    end, {owner_id})
end

--
-- Server 1 was a synchro queue owner. Then it receives a foreign PROMOTE which
-- goes to WAL but is not applied yet. Server 1 during that tries to make a
-- synchro transaction. It is expected to be aborted at commit attempt, because
-- the queue is already in the process of ownership transition.
--
g.test_local_txn_during_remote_promote = function(g)
    -- Server 1 takes the synchro queue.
    g.server1:exec(function()
        box.ctl.promote()
        box.cfg{
            -- To hang own transactions in the synchro queue.
            replication_synchro_quorum = 3,
        }
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        -- Other server will send a promote - it should get stuck in the WAL.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    -- Deliver server 1 promotion to 2. Otherwise server 2 might fail trying to
    -- start its own promotion simultaneously.
    g.server2:wait_vclock_of(g.server1)

    -- Server 2 sends PROMOTE to server 1.
    g.server2:exec(function()
        require('fiber').create(box.ctl.promote)
    end)

    -- PROMOTE is stuck in the WAL on server 1.
    play_wal_until_synchro_queue_is_busy(g.server1)

    -- Server 1 shouldn't be able to make new transactions while a foreign
    -- PROMOTE goes to WAL.
    local ok1, err1, ok2, err2 = g.server1:exec(function()
        local s = box.space.test
        local fiber = require('fiber')
        -- More than one transaction to ensure that it works not just for one.
        local f1 = fiber.new(s.replace, s, {1})
        f1:set_joinable(true)
        local f2 = fiber.new(s.replace, s, {2})
        f2:set_joinable(true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        -- Multi-return of form `join(), join()` doesn't work as one might
        -- expect. Do it manually.
        local ok1, err1 = f1:join()
        local ok2, err2 = f2:join()
        return ok1, err1, ok2, err2
    end)
    luatest.assert(not ok1 and not ok2 and err1 and err2,
                   'both transactions failed')
    luatest.assert_equals(err1.code, err2.code, 'same error')
    luatest.assert_equals(err1.code, box.error.SYNC_ROLLBACK,
                          'error is synchro rollback')

    -- Server 1 correctly processed the remote PROMOTE.
    wait_synchro_owner(g.server1, g.server2:instance_id())

    -- The synchronous replication is functional - new owner can use the queue.
    g.server2:exec(function()
        box.space.test:replace{3}
    end)
    g.server1:wait_vclock_of(g.server2)
    local content = g.server1:exec(function()
        return box.space.test:select{}
    end)
    luatest.assert_equals(content, {{3}}, 'synchro transactions work')
end
