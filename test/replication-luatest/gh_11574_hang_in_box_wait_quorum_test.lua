local t = require("luatest")
local server = require("luatest.server")
local replicaset = require("luatest.replica_set")

local g = t.group()

g.before_all(function(g)
    g.replicaset = replicaset:new({})
    local full_replication = {
        server.build_listen_uri("server1", g.replicaset.id),
        server.build_listen_uri("server2", g.replicaset.id),
        server.build_listen_uri("server3", g.replicaset.id),
    }
    local replica_box_cfg = {
        replication = full_replication,
        read_only = true,
        election_mode = "manual",
        replication_timeout = 0.5,
    }

    g.server1 = g.replicaset:build_and_add_server({
        alias = "server1",
        box_cfg = {
            replication = full_replication,
            replication_synchro_quorum = 4,
            replication_timeout = 0.5,
        },
    })
    g.server2 = g.replicaset:build_and_add_server({
        alias = "server2",
        box_cfg = replica_box_cfg,
    })
    g.server3 = g.replicaset:build_and_add_server({
        alias = "server3",
        box_cfg = replica_box_cfg,
    })

    g.replicaset:start()
    g.replicaset:wait_for_fullmesh()
    g.server1:exec(function()
        require('compat').replication_synchro_timeout = 'new'
        box.schema.create_space("s", {is_sync = true}):create_index("p")
    end)
end)

g.after_each(function(g)
    g.server1:exec(function() t.assert_equals(box.info.ro, false) end)
    g.server1:update_box_cfg({replication_synchro_quorum = 4})
end)

g.after_all(function(g)
    g.replicaset:drop()
end)

local function capture_synchro_queue_and_push_sync_transaction(server)
    server:exec(function()
        local fiber = require("fiber")
        -- We should set election_mode to "off" in order to capture the
        -- synchronous queue and store a sync transaction into it. It is
        -- important that the transaction should not be committed before
        -- a successful box.ctl.promote.
        box.cfg{election_mode = "off"}
        box.ctl.promote()
        t.assert_equals(box.info.election.state, "follower")
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.queue.len, 0)
        box.atomic({wait = "submit"}, function() box.space.s:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        -- So that we have the opportunity to perform some actions during
        -- hang in box_wait_quorum, we wrap the last box.ctl.promote into
        -- fiber and join it after some actions. In our case this action
        -- is reconfiguring of replication_synchro_quorum.
        box.cfg{election_mode = "manual"}
        rawset(_G, "raft_worker_f", fiber.new(function()
            box.ctl.promote()
            t.assert_equals(box.info.election.state, "leader")
        end))
        _G.raft_worker_f:set_joinable(true)
    end)
end

local function wait_until_synchro_queue_is_empty(server)
    -- In some rare cases the limbo may not be cleared in time after
    -- successful completion of box_wait_quorum. We should wrap it
    -- into retrying block.
    server:exec(function()
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
    end)
end

g.test_box_wait_quorum_while_changing_replication_synchro_quorum = function(g)
    g.server1:update_box_cfg({replication_synchro_quorum = 4})
    capture_synchro_queue_and_push_sync_transaction(g.server1)
    g.server1:exec(function()
        -- It is necessary to wait until the g.server1 enters the leader state,
        -- because otherwise the reconfiguration may appear earlier than
        -- the invocation of the box_wait_quorum with higher quorum - 4.
        -- The reconfiguration must be performed strctly after box_wait_quorum
        -- starts and hangs.
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.election.state, "leader")
        end)
        box.cfg{replication_synchro_quorum = 3}
        _G.raft_worker_f:join()
    end)
    wait_until_synchro_queue_is_empty(g.server1)
end

g.test_box_wait_quorum_by_cluster_size_replication_synchro_quorum = function(g)
    g.server1:update_box_cfg({replication_synchro_quorum = 3})
    capture_synchro_queue_and_push_sync_transaction(g.server1)
    g.server1:exec(function() _G.raft_worker_f:join() end)
    wait_until_synchro_queue_is_empty(g.server1)
end

g.test_box_wait_quorum_with_lower_replication_synchro_quorum = function(g)
    g.server1:update_box_cfg({replication_synchro_quorum = 2})
    capture_synchro_queue_and_push_sync_transaction(g.server1)
    g.server1:exec(function() _G.raft_worker_f:join() end)
    wait_until_synchro_queue_is_empty(g.server1)
end
