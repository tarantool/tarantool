local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('replication_synchro_queue_max_size')
--
-- gh-7486: introduce `replication_synchro_queue_max_size`.
--
local wait_timeout = 10

local function wait_pair_sync(server1, server2)
    -- Without retrying it fails sometimes when vclocks are empty and both
    -- instances are in 'connect' state instead of 'follow'.
    t.helpers.retrying({timeout = wait_timeout}, function()
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
        replication_synchro_quorum = 2,
        replication_synchro_timeout = 100000,
        replication_timeout = 0.1,
        election_fencing_mode = 'off',
    }
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = box_cfg,
    })
    box_cfg.election_mode = 'voter'
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

g.test_master_synchro_queue_limited = function(cg)
    cg.master:exec(function(wait_timeout)
        box.cfg{
            replication_synchro_queue_max_size = 1,
            replication_synchro_quorum = 3,
        }
        local f = require('fiber')
            .new(pcall, box.space.test.insert, box.space.test, {1})
        f:set_joinable(true)

        t.helpers.retrying({timeout = wait_timeout}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)

        local ok, err = pcall(box.space.test.insert, box.space.test, {2})
        t.assert(not ok)
        t.assert_equals(err.type, 'ClientError')
        t.assert_equals(err.message, 'Synchro queue is full')

        box.cfg{replication_synchro_quorum = 2}
        local _, ok = f:join()
        t.assert(ok)
    end, {wait_timeout})
end

g.test_size_is_updated_correctly_after_commit = function(cg)
    cg.master:exec(function()
        box.cfg{replication_synchro_queue_max_size = 1}
        local ok, _ = pcall(box.space.test.insert, box.space.test, {1})
        t.assert(ok)
        ok, _ = pcall(box.space.test.insert, box.space.test, {2})
        t.assert(ok)
    end)
end

g.test_size_is_updated_correctly_after_rollback = function(cg)
    cg.master:exec(function()
        box.cfg{
            replication_synchro_queue_max_size = 1,
            replication_synchro_quorum = 3,
            replication_synchro_timeout = 0.001,
        }
        local ok, err = pcall(box.space.test.insert, box.space.test, {1})
        t.assert(not ok)
        t.assert_equals(err.type, 'ClientError')
        t.assert_equals(err.message, 'Quorum collection for a synchronous ' ..
            'transaction is timed out')

        box.cfg{replication_synchro_quorum = 2}
        local ok, _ = pcall(box.space.test.insert, box.space.test, {2})
        t.assert(ok)
    end)
end
