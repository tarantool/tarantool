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

local function block_server_on_box_wait_limbo_acked(server)
    server:exec(function()
        local wait_quorum_count =
            box.error.injection.get('ERRINJ_WAIT_QUORUM_COUNT')
        box.atomic({wait = 'submit'}, function() box.space.s:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        box.cfg{election_mode = 'manual'}
        require('fiber').new(function()
            box.ctl.promote()
        end)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_gt(box.error.injection.get('ERRINJ_WAIT_QUORUM_COUNT'),
                        wait_quorum_count)
        end)
    end)
end

-- This test covers the `box_wait_limbo_acked` failure path.
g.test_wait_limbo_acked_failure = function(cg)
    t.tarantool.skip_if_not_debug()

    block_server_on_box_wait_limbo_acked(cg.replica1)

    local term = cg.replica1:get_synchro_queue_term()

    cg.replica2:exec(function()
        box.cfg{replication_synchro_quorum = 1}
        box.ctl.promote()
        box.ctl.wait_rw()
    end)

    cg.replica1:exec(function(term)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_gt(box.info.synchro.queue.term, term)
        end)
        t.assert_equals(box.info.synchro.queue.len, 0)
    end, {term})

    cg.replica2:exec(function()
        box.cfg{replication_synchro_quorum = 3}
        box.atomic({wait = 'submit'}, function() box.space.s:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)

    cg.replica1:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)

        box.cfg{replication_synchro_quorum = 2}
    end)

    local msg = "Instance with replica id %d was promoted first"
    t.helpers.retrying({timeout = 120}, function()
        t.assert(cg.replica1:grep_log(msg))
    end)
end

-- This test covers the `raft->state != RAFT_STATE_LEADER` check path.
g.test_is_leader_check = function(cg)
    t.tarantool.skip_if_not_debug()

    block_server_on_box_wait_limbo_acked(cg.replica1)

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
        t.assert_equals(box.info.synchro.queue.len, 1)

        box.cfg{replication_synchro_quorum = 2}
    end, {term})

    local msg = "The instance is not a leader. New leader is %d"
    t.helpers.retrying({timeout = 120}, function()
        t.assert(cg.replica1:grep_log(msg))
    end)
end

-- This test covers the `box_issue_promote` failure path.
g.test_box_issue_promote_failure = function(cg)
    t.tarantool.skip_if_not_debug()

    block_server_on_box_wait_limbo_acked(cg.replica1)

    cg.replica1:exec(function()
        box.error.injection.set("ERRINJ_TXN_LIMBO_BEGIN_DELAY_COUNTDOWN", 0)
        box.cfg{replication_synchro_quorum = 2}

        t.helpers.retrying({timeout = 120}, function()
            t.assert(box.error.injection.get("ERRINJ_TXN_LIMBO_BEGIN_DELAY"))
        end)
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
