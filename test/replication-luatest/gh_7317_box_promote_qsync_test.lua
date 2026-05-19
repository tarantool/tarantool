local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('replica1', cg.replica_set.id),
            server.build_listen_uri('replica2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_timeout = 120,
        replication_synchro_quorum = 3,
    }
    cg.replica1 = cg.replica_set:build_and_add_server{
        alias = 'replica1',
        box_cfg = box_cfg,
    }
    box_cfg.election_mode = 'manual'
    box_cfg.read_only = true
    cg.replica2 = cg.replica_set:build_and_add_server{
        alias = 'replica2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.replica1:exec(function()
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.ctl.promote()
    end)
    cg.replica2:exec(function()
        box.cfg{read_only = false}
    end)
    cg.replica1:wait_for_downstream_to(cg.replica2)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

--
-- Make the server start a promotion which can't finish. It wins the elections
-- and writes its PROMOTE, but the PROMOTE confirms a pending transaction, so
-- it needs the synchro quorum, which is unreachable.
--
local function block_server_on_promote_quorum(server)
    server:exec(function()
        box.atomic({wait = 'submit'}, function() box.space.s:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        box.cfg{election_mode = 'manual'}
        local f = require('fiber').new(function()
            box.ctl.demote()
            local ok, err = pcall(box.ctl.promote)
            return {ok, err}
        end)
        f:set_joinable(true)
        rawset(_G, 'promote_fiber', f)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_not_equals(box.info.synchro.own_promote, nil)
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
end

-- Join the promotion started by block_server_on_promote_quorum(), expecting it
-- to fail with the given error code.
local function join_failed_promote(server, code)
    server:exec(function(code)
        local ok, res = rawget(_G, 'promote_fiber'):join(120)
        t.assert(ok)
        local promote_ok, promote_err = unpack(res)
        t.assert_not(promote_ok)
        t.assert_equals(promote_err.code, code)
    end, {code})
end

-- The own PROMOTE's quorum wait is interrupted by another instance's promotion,
-- which takes the queue over.
g.test_promote_quorum_wait_interrupted_by_promote = function(cg)
    t.tarantool.skip_if_not_debug()

    block_server_on_promote_quorum(cg.replica1)

    local term = cg.replica1:get_synchro_queue_term()

    cg.replica2:exec(function()
        box.cfg{replication_synchro_quorum = 1}
        box.ctl.promote()
        box.ctl.wait_rw()
    end)

    -- The new leader's PROMOTE covers the pending transaction and drops the
    -- old leader's own promotion.
    cg.replica1:exec(function(term)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_gt(box.info.synchro.queue.term, term)
        end)
        t.assert_equals(box.info.synchro.queue.len, 0)
        t.assert_equals(box.info.synchro.own_promote, nil)
    end, {term})
    join_failed_promote(cg.replica1, box.error.INTERFERING_ELECTIONS)

    -- The replication keeps working after the failed promotion.
    cg.replica2:exec(function()
        box.cfg{replication_synchro_quorum = 3}
        box.atomic({wait = 'submit'}, function() box.space.s:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
    cg.replica1:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
    end)
end

-- The own PROMOTE's quorum wait is interrupted by losing the leadership.
g.test_promote_quorum_wait_interrupted_by_elections = function(cg)
    t.tarantool.skip_if_not_debug()

    block_server_on_promote_quorum(cg.replica1)

    local term = cg.replica1:get_election_term()

    cg.replica2:exec(function()
        require('fiber').new(function()
            box.ctl.promote()
        end)
    end)

    -- The new leader's PROMOTE stays pending too - the quorum is unreachable
    -- for it as well. The old leader's transaction stays in the queue.
    cg.replica1:exec(function(term)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_gt(box.info.election.term, term)
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
    end, {term})
    join_failed_promote(cg.replica1, box.error.INTERFERING_ELECTIONS)
end

-- The PROMOTE preparation fails, because the elections interfere while the
-- promotion is waiting for the limbo latch.
g.test_promote_prepare_interfering_elections = function(cg)
    t.tarantool.skip_if_not_debug()

    cg.replica1:exec(function()
        box.error.injection.set("ERRINJ_TXN_LIMBO_BEGIN_DELAY_COUNTDOWN", 0)
        box.cfg{election_mode = 'manual'}
        require('fiber').new(function()
            box.ctl.demote()
            box.ctl.promote()
        end)
        -- The elections are won, and the promotion is stuck right before
        -- taking the limbo latch.
        t.helpers.retrying({timeout = 120}, function()
            t.assert(box.error.injection.get("ERRINJ_TXN_LIMBO_BEGIN_DELAY"))
        end)
        t.assert_equals(box.info.election.state, 'leader')
    end)

    local term = cg.replica1:get_election_term()

    cg.replica2:exec(function()
        require('fiber').new(function()
            box.ctl.promote()
        end)
    end)

    cg.replica1:exec(function(term)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_gt(box.info.election.term, term)
        end)
        box.error.injection.set("ERRINJ_TXN_LIMBO_BEGIN_DELAY", false)
    end, {term})

    local msg = "Interfering elections started"
    t.helpers.retrying({timeout = 120}, function()
        t.assert(cg.replica1:grep_log(msg))
    end)
end
