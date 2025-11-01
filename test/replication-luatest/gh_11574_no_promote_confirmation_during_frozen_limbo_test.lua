local t = require("luatest")
local server = require("luatest.server")
local replicaset = require("luatest.replica_set")

local g = t.group()

g.before_all(function(g)
    g.replicaset = replicaset:new({})
    local box_cfg = {
        replication = {
            server.build_listen_uri("server1", g.replicaset.id),
            server.build_listen_uri("server2", g.replicaset.id),
        },
        replication_synchro_quorum = 2,
        replication_timeout = 0.1,
        election_mode = "manual",
    }

    g.server1 = g.replicaset:build_and_add_server({
        alias = "server1",
        box_cfg = box_cfg,
    })
    box_cfg.read_only = true
    g.server2 = g.replicaset:build_and_add_server({
        alias = "server2",
        box_cfg = box_cfg,
    })

    g.replicaset:start()
    g.replicaset:wait_for_fullmesh()
    g.server1:wait_for_election_leader()
    g.server1:exec(function()
        require('compat').replication_synchro_timeout = 'new'
        box.schema.create_space("s", {is_sync = true}):create_index("p")
    end)
    g.server1:wait_for_vclock_of(g.server2)
    g.server2:wait_for_vclock_of(g.server1)
end)

g.after_all(function(g)
    g.replicaset:drop()
end)

g.test_no_promote_confirmation_during_frozen_limbo = function(g)
    g.server2:exec(function()
        box.error.injection.set("ERRINJ_WAL_IO", true)
    end)
    g.server1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.election.state, "leader")
        t.assert_equals(box.info.election.term, 2)
        box.atomic({wait = "submit"}, function() box.space.s:replace{0} end)
    end)
    -- Wait until node switches into fencing mode
    g.server1:exec(function()
        t.helpers.retrying({}, function()
            t.assert(box.info.ro)
            t.assert_equals(box.info.ro_reason, "election")
        end)
    end)
    g.server1:exec(function()
        box.cfg{replication_synchro_quorum = 1}
        t.assert_equals(box.space.s:select(), {{0}})
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
    g.server2:exec(function()
        t.assert_equals(box.space.s:select(), {})
        t.assert_equals(box.info.synchro.queue.len, 0)
    end)
end
