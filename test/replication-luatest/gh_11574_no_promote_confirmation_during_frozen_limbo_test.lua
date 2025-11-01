local t = require("luatest")
local server = require("luatest.server")
local replicaset = require("luatest.replica_set")

local g = t.group()

g.before_all(function(g)
    t.tarantool.skip_if_not_debug()
    g.replicaset = replicaset:new({})
    local box_cfg = {
        replication = {
            server.build_listen_uri("server1", g.replicaset.id),
            server.build_listen_uri("server2", g.replicaset.id),
        },
        replication_synchro_quorum = 2,
        replication_timeout = 0.1,
        replication_synchro_timeout = 1000,
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
        box.ctl.promote()
        box.schema.create_space("s", {is_sync = true}):create_index("p")
    end)
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
        local f = require('fiber').create(function()
            box.space.s:replace{0}
        end)
        f:set_joinable(true)
        -- Wait until node switches into fencing mode
        t.helpers.retrying({}, function()
            t.assert(box.info.ro)
            t.assert_equals(box.info.ro_reason, "election")
        end)
        box.cfg{replication_synchro_quorum = 1}
        t.assert_not(f:join(1))
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
    g.server2:exec(function(server1_id)
        t.assert_equals(box.info.synchro.queue.len, 0)
        box.error.injection.set("ERRINJ_WAL_IO", false)
        local old_replication = box.cfg.replication
        box.cfg{replication = {}}
        box.cfg{replication = old_replication}
        t.helpers.retrying({}, function()
            local rs = box.info.replication[server1_id]
            t.assert(rs.upstream and rs.downstream)
            t.assert_equals(rs.upstream.status, 'follow')
            t.assert_equals(rs.downstream.status, 'follow')
        end)
    end, {g.server1:get_instance_id()})

    g.server1:exec(function()
        local res, err = pcall(box.ctl.promote)
        t.assert(res)
        t.assert_not(err)
        t.helpers.retrying({}, function()
            t.assert_not(box.info.ro)
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
    end)
    g.server2:exec(function()
        t.helpers.retrying({}, function()
            t.assert_equals(box.space.s:select(), {{0}})
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
    end)
end
