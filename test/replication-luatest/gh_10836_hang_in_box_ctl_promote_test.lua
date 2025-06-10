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

    g.server1 = g.replicaset:build_and_add_server({
        alias = "server1",
        box_cfg = {
            election_mode = "voter",
            replication = { g.uri1, g.proxy_uri_1_to_2, g.proxy_uri_1_to_3 }
        }
    })
    g.server2 = g.replicaset:build_and_add_server({
        alias = "server2",
        box_cfg = {
            election_mode = "manual",
            replication = { g.proxy_uri_2_to_1, g.uri2, g.uri3 }
        }
    })
    g.server3 = g.replicaset:build_and_add_server({
        alias = "server3",
        box_cfg = {
            election_mode = "voter",
            replication = { g.proxy_uri_3_to_1, g.uri2, g.uri3 }
        }
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
    g.server2:wait_for_election_leader()
end)

g.after_all(function()
    g.replicaset:drop()
end)

g.test_promote_not_hangs_during_non_leader_message_about_leader = function()
    t.assert_equals(g.server1:get_election_term(),
        g.server3:get_election_term())

    g.proxy_2_to_1:pause()
    g.proxy_3_to_1:pause()
    g.proxy_1_to_2:pause()
    g.proxy_1_to_3:pause()

    g.server2:update_box_cfg({election_mode = "voter"})
    g.server3:exec(function()
        box.cfg({election_mode = "manual"})
        local _, err = pcall(box.ctl.promote)
        t.assert_equals(err, nil)
    end)

    t.assert_lt(g.server1:get_election_term(),
        g.server3:get_election_term())
    g.server3:exec(function()
        t.assert(box.info.election.state == "leader")
    end)

    g.server1:exec(function()
        local fiber = require("fiber")
        -- It is necessary to update election_mode before box.ctl.promote
        -- because without this action the raft state machine will change
        -- its state into "follower" when the server reconfigures while
        -- waiting (reconnecting to g.server2). This behaviour breaks the
        -- test, because box.ctl.promote will not hang as it expects without
        -- the patch.
        box.cfg({election_mode = "candidate"})
        rawset(_G, "server1_promote", fiber.new(function()
            return pcall(box.ctl.promote)
        end))
        _G.server1_promote:set_joinable(true)
    end)

    t.assert_equals(g.server1:get_election_term(),
        g.server2:get_election_term())

    g.proxy_2_to_1:resume()
    g.proxy_1_to_2:resume()

    g.server1:exec(function()
        local is_success, pcall_res, pcall_err = _G.server1_promote:join()
        local pcall_err_wrapper = setmetatable(pcall_err:unpack(), {
            __tostring = require("json").encode})
        t.assert(is_success)
        t.assert_not(pcall_res)
        t.assert_equals(pcall_err_wrapper.code, 0)
    end)
end
