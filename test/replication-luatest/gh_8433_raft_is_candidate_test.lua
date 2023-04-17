local luatest = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local proxy = require('luatest.replica_proxy')

local g = luatest.group('is_candidate_fail')

local function wait_for_upstream_death_with_id(id)
    luatest.helpers.retrying({}, function()
        luatest.assert_equals(box.info.replication[id].upstream.status,
                              'disconnected')
    end)
end

g.before_all(function(g)
    g.replica_set = replica_set:new({})
    local rs_id = g.replica_set.id
    g.box_cfg = {
        election_mode = 'candidate',
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('server1', rs_id),
            server.build_listen_uri('server2', rs_id),
            server.build_listen_uri('server3', rs_id),
        },
    }

    g.server1 = g.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = g.box_cfg,
    }

    -- Force server1 to be a leader for reliability and reproducibility
    g.box_cfg.election_mode = 'voter'
    g.server2 = g.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = g.box_cfg,
    }

    g.proxy1 = proxy:new{
        client_socket_path = server.build_listen_uri('server1_proxy'),
        server_socket_path = server.build_listen_uri('server1', rs_id),
    }

    g.proxy2 = proxy:new{
        client_socket_path = server.build_listen_uri('server2_proxy'),
        server_socket_path = server.build_listen_uri('server2', rs_id),
    }

    luatest.assert(g.proxy1:start{force = true}, 'Proxy from 3 to 1 started')
    luatest.assert(g.proxy2:start{force = true}, 'Proxy from 3 to 2 started')
    g.box_cfg.replication[1] = server.build_listen_uri('server1_proxy')
    g.box_cfg.replication[2] = server.build_listen_uri('server2_proxy')

    g.server3 = g.replica_set:build_and_add_server{
        alias = 'server3',
        box_cfg = g.box_cfg,
    }

    g.replica_set:start()
    g.replica_set:wait_for_fullmesh()
end)

g.after_all(function(g)
    g.replica_set:drop()
end)

g.test_prevote_fail = function(g)
    --
    -- Test that applier failure doesn't start election if the node
    -- doesn't have enough quorum for doing that:
    --     1. Fill leader_witness_map in order to not allow election_update_cb
    --        start election on timeout.
    --     2. Break applier to leader and wait until it dies and
    --        the corresponding bit in leader_witness_map is cleared.
    --     3. Break last applier and make sure, that election isn't started.
    --
    luatest.assert_equals(g.replica_set:get_leader(), g.server1)
    local old_term = g.server1:get_election_term()
    g.server3:exec(function()
        box.cfg({election_mode = 'candidate'})
    end)

    -- We must be sure, that the server3 cannot start elections on timeout.
    -- It's witness map should not be empty, when update_election_cb is executed
    luatest.helpers.retrying({}, function()
        if not g.server3:grep_log('leader is seen: true, state: follower') then
            error("Witness map may still be empty")
        end
    end)

    g.proxy1:pause()

    -- Wait for the server3 to notice leader death and clear the
    -- corresponding bit in leader_witness_map. The elections are not
    -- supposed to start as the server2 says, that it can see the leader.
    local id1 = g.server1:get_instance_id()
    g.server3:exec(wait_for_upstream_death_with_id, {id1})

    g.proxy2:pause()

    -- server3 should not start elections, as it doesn't
    -- have enough quorum of healthy nodes to do that.
    local id2 = g.server2:get_instance_id()
    g.server3:exec(wait_for_upstream_death_with_id, {id2})
    luatest.assert_equals(g.server3:get_election_term(), old_term)

    -- Restore appliers
    g.proxy1:resume()
    g.proxy2:resume()
    -- Make sure that replica set is stable
    luatest.assert_equals(g.server3:get_election_term(), old_term)
    luatest.assert_equals(g.server1:get_election_term(),
                          g.server3:get_election_term())
end
