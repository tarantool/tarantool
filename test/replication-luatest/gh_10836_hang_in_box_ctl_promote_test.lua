local server = require("luatest.server")
local replica_set = require("luatest.replica_set")
local proxy = require("luatest.replica_proxy")
local t = require("luatest")

local g = t.group()

g.before_all(function()
    g.replicaset = replica_set:new({})

    g.uri1 = server.build_listen_uri("server1", g.replicaset.id)
    g.uri2 = server.build_listen_uri("server2", g.replicaset.id)
    g.uri3 = server.build_listen_uri("server3", g.replicaset.id)

    g.proxy_uri_1_to_2 = server.build_listen_uri(
        "proxy_1_to_2", g.replicaset.id)
    g.proxy_uri_1_to_3 = server.build_listen_uri(
        "proxy_1_to_3", g.replicaset.id)
    g.proxy_uri_2_to_1 = server.build_listen_uri(
        "proxy_2_to_1", g.replicaset.id)
    g.proxy_uri_3_to_1 = server.build_listen_uri(
        "proxy_3_to_1", g.replicaset.id)

    g.box_cfg = {
        election_mode = "manual",
        replication_timeout = 0.5,
        -- One of the tests reaches a TimedOut condition, so lessen the timeout
        -- to make the test work faster.
        election_timeout = 2
    }

    g.box_cfg.replication = { g.uri1, g.proxy_uri_1_to_2, g.proxy_uri_1_to_3 }
    g.server1 = g.replicaset:build_and_add_server({
        alias = "server1",
        box_cfg = g.box_cfg
    })

    g.box_cfg.replication = { g.proxy_uri_2_to_1, g.uri2, g.uri3 }
    g.server2 = g.replicaset:build_and_add_server({
        alias = "server2",
        box_cfg = g.box_cfg
    })

    g.box_cfg.replication = { g.proxy_uri_3_to_1, g.uri2, g.uri3 }
    g.server3 = g.replicaset:build_and_add_server({
        alias = "server3",
        box_cfg = g.box_cfg
    })

    -- We use 4 proxies in order to emulate a bidirectional connection
    -- break and reconnecting between g.server1 and other servers.
    g.proxy_1_to_2 = proxy:new({
        client_socket_path = g.proxy_uri_1_to_2,
        server_socket_path = g.uri2})
    g.proxy_1_to_3 = proxy:new({
        client_socket_path = g.proxy_uri_1_to_3,
        server_socket_path = g.uri3})
    g.proxy_2_to_1 = proxy:new({
        client_socket_path = g.proxy_uri_2_to_1,
        server_socket_path = g.uri1})
    g.proxy_3_to_1 = proxy:new({
        client_socket_path = g.proxy_uri_3_to_1,
        server_socket_path = g.uri1})

    t.assert(g.proxy_2_to_1:start())
    t.assert(g.proxy_3_to_1:start())
    t.assert(g.proxy_1_to_2:start())
    t.assert(g.proxy_1_to_3:start())

    g.replicaset:start()
    g.replicaset:wait_for_fullmesh()
    g.server2:exec(function() box.ctl.promote() end)
    g.server2:wait_for_election_leader()
end)

g.after_all(function()
    g.replicaset:drop()
end)

g.test_promote_not_hangs_during_non_leader_message_about_leader = function()
    t.helpers.retrying({}, function()
        t.assert_equals(g.server1:get_election_term(),
                        g.server3:get_election_term())
    end)

    g.proxy_2_to_1:pause()
    g.proxy_3_to_1:pause()
    g.proxy_1_to_2:pause()
    g.proxy_1_to_3:pause()

    g.server3:exec(function()
        local _, err = pcall(box.ctl.promote)
        t.assert_equals(err, nil)
    end)

    t.assert_lt(g.server1:get_election_term(),
                g.server3:get_election_term())
    g.server3:wait_for_election_leader()

    g.server1:exec(function()
        local fiber = require("fiber")
        -- It is important to set election_mode into "candidate" because
        -- without this action the box.ctl.promote will not reproduce hang
        -- before the patch as it expects. The reason for this is that
        -- raft->is_cfg_candidate is set to false with election_mode "manual"
        -- and after the trigger raft_sm_election_update_cb is invoked the
        -- state of our raft state machine is set to "follower". We expect
        -- that state should be "candidate".
        box.cfg({election_mode = "candidate"})
        rawset(_G, "server1_promote", fiber.new(function()
            return pcall(box.ctl.promote)
        end))
        _G.server1_promote:set_joinable(true)
    end)

    t.helpers.retrying({}, function()
        t.assert_equals(g.server1:get_election_term(),
                        g.server2:get_election_term())
    end)

    g.proxy_2_to_1:resume()
    g.proxy_1_to_2:resume()

    g.server1:exec(function()
        local is_success, pcall_res, pcall_err = _G.server1_promote:join()
        t.assert(is_success)
        t.assert_not(pcall_res)
        t.assert_equals(pcall_err.type, "TimedOut")
    end)
end

g.test_node_not_wait_promote_timeout_after_fiber_death = function()
    g.server2:exec(function() box.ctl.promote() end)
    g.server2:wait_for_election_state("leader")

    g.server3:exec(function(promote_timeout)
        local fiber = require("fiber")
        local promote_f = fiber.new(function()
            return pcall(box.ctl.promote)
        end)
        promote_f:set_joinable(true)
        promote_f:cancel()
        fiber.yield()

        local start_time = fiber.clock()
        local is_success, res, err = promote_f:join()
        t.assert_lt(fiber.clock() - start_time, promote_timeout)
        t.assert(is_success)
        t.assert_not(res)
        t.assert_equals(err.type, "FiberIsCancelled")
    end, {g.box_cfg.election_timeout})
end
