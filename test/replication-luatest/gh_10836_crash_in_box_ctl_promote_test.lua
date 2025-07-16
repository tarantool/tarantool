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

    local box_cfg = {
        election_mode = "manual",
        replication_timeout = 0.5,
        election_timeout = 2,
    }

    box_cfg.replication = {g.uri1, g.proxy_uri_1_to_2, g.proxy_uri_1_to_3}
    g.server1 = g.replicaset:build_and_add_server(
        {alias = "server1", box_cfg = box_cfg})
    box_cfg.replication = {g.proxy_uri_2_to_1, g.uri2, g.uri3}
    g.server2 = g.replicaset:build_and_add_server(
        {alias = "server2", box_cfg = box_cfg})
    box_cfg.replication = {g.proxy_uri_3_to_1, g.uri2, g.uri3}
    g.server3 = g.replicaset:build_and_add_server(
        {alias = "server3", box_cfg = box_cfg})

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

g.test_node_not_crashes_while_gaining_quorum_during_promote = function()
    t.assert_equals(g.server1:get_election_term(),
                    g.server3:get_election_term())

    g.proxy_2_to_1:pause()
    g.proxy_3_to_1:pause()
    g.proxy_1_to_2:pause()
    g.proxy_1_to_3:pause()

    g.server3:exec(function()
        local _, err = pcall(box.ctl.promote)
        t.assert_equals(err, nil)
    end)
    g.server3:wait_for_election_leader()
    t.assert_lt(g.server1:get_election_term(),
                g.server3:get_election_term())

    g.server1:exec(function()
        local f = require('fiber')
        -- Do not allow promote to end.
        box.error.injection.set('ERRINJ_RAFT_PROMOTE_DELAY', true)
        rawset(_G, 'promote_f', f.new(function()
            return pcall(box.ctl.promote)
        end))
        _G.promote_f:set_joinable(true)

        -- Break replication, quorum loss, is_candidate = false.
        box.cfg{replication = {}}
    end)

    g.proxy_2_to_1:resume()
    g.proxy_1_to_2:stop()

    g.server1:exec(function(repl, id)
        -- Before control returns to box_raft_try_promote quorum is gained.
        box.cfg{replication = repl}
        t.helpers.retrying({}, function()
            local rs = box.info.replication[id]
            t.assert(rs.upstream and rs.downstream)
            t.assert_equals(rs.upstream.status, 'follow')
            t.assert_equals(rs.downstream.status, 'follow')
        end)

        -- Any `raft_restore` works, which sets `is_candidate` to `true`.
        -- Start timer, so that restore is called.
        box.cfg{election_mode = 'candidate'}
        box.error.injection.set('ERRINJ_RAFT_PROMOTE_DELAY', false)

        local ok, pcall_ok, pcall_err = _G.promote_f:join()
        t.assert(ok)
        t.assert_not(pcall_ok)
        t.assert_equals(pcall_err.code, box.error.NO_ELECTION_QUORUM)
    end, {{g.uri2, g.uri1}, g.server2:get_instance_id()})
end
