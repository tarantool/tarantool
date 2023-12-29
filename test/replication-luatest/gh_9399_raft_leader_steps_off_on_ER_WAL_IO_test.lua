local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('raft-leader-steps-off-on-ER_WAL_IO')

local function wait_pair_sync(server1, server2)
    -- Without retrying it fails sometimes when vclocks are empty and both
    -- instances are in 'connect' state instead of 'follow'.
    t.helpers.retrying({timeout = 10}, function()
        server1:wait_for_vclock_of(server2)
        server2:wait_for_vclock_of(server1)
        server1:assert_follows_upstream(server2:get_instance_id())
        server2:assert_follows_upstream(server1:get_instance_id())
    end)
end

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()

    cg.cluster = cluster:new({})

    local box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
            server.build_listen_uri('replica', cg.cluster.id),
        },
        election_mode = 'candidate',
        replication_synchro_quorum = 1,
        replication_synchro_timeout = 100000,
        replication_timeout = 0.1,
        election_fencing_mode = 'off',
    }
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = box_cfg
    })
    box_cfg.election_mode = 'off'
    cg.replica = cg.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = box_cfg
    })
    cg.cluster:start()
    cg.master:wait_until_election_leader_found()
    cg.replica:wait_until_election_leader_found()

    cg.master:exec(function()
        box.schema.space.create('test', {is_sync = true})
        box.space.test:create_index('pk')
    end)
    wait_pair_sync(cg.replica, cg.master)
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

g.test_cluster_elect_new_leader = function(cg)
    cg.replica:exec(function()
        box.cfg{election_mode='candidate'}
    end)
    cg.master:exec(function()
        t.assert_equals(box.info.election.state, 'leader')
        box.error.injection.set('ERRINJ_WAL_IO', true)
        local ok, err = pcall(box.space.test.insert, box.space.test, {1})
        t.assert(not ok)
        t.assert_equals(err.type, 'ClientError')
        t.assert_equals(err.message, 'Failed to write to disk')
    end)
    cg.replica:wait_for_election_state('leader')
    -- Master failed on writing current raft state.
    t.helpers.retrying({}, function()
        cg.replica:grep_log("Could not write a raft request to WAL")
    end)
end

-- Check that instance panics only if it fails to write the raft state.
-- If the disk is repaired immediately after one unsuccessful write attempt,
-- instance continues to work normally.
g.test_leader_continues_work_after_one_write_error = function(cg)
    local term = cg.master:get_election_term()
    cg.master:exec(function()
        t.assert_equals(box.info.election.state, 'leader')
        box.error.injection.set('ERRINJ_WAL_IO', true)
        -- Only one write attempt fails.
        box.error.injection.set('ERRINJ_WAL_IO_COUNTDOWN', 0)
        local ok, err = pcall(box.space.test.insert, box.space.test, {1})
        t.assert(not ok)
        t.assert_equals(err.type, 'ClientError')
        t.assert_equals(err.message, 'Failed to write to disk')
    end)
    -- Master continued to work because the raft state successfully written.
    cg.master:wait_for_election_term(term + 1)
    cg.master:wait_for_election_state('leader')
end
