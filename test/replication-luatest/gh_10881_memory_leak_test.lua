local server = require("luatest.server")
local cluster = require("luatest.replica_set")
local t = require("luatest")

local g = t.group('gh_asan_leak_on_shutdown')

g.before_each(function(g)
    t.tarantool.skip_if_not_debug()

    g.cluster = cluster:new{}

    local box_cfg = {
        replication = {
            server.build_listen_uri("master", g.cluster.id),
            server.build_listen_uri("replica", g.cluster.id),
        },
    }
    g.master = g.cluster:build_and_add_server({
        alias = "master",
        box_cfg = box_cfg,
    })
    box_cfg.read_only = true
    g.replica = g.cluster:build_and_add_server({
        alias = "replica",
        box_cfg = box_cfg,
    })
    g.cluster:start()
end)

g.after_each(function(g)
    g.cluster:drop()
end)

g.test_replica_shutdown_leak = function()
    local wal_cnt = g.replica:exec(function()
        return box.error.injection.get('ERRINJ_WAL_WRITE_COUNT')
    end)
    -- Make sure that the next running transaction freezes.
    g.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_DURATION', 2)
    end)

    local replica_id = g.replica:get_instance_id()
    -- Starting a transaction that will take a long time to complete.
    g.master:exec(function(replica_id)
        box.space._cluster:delete{replica_id}
    end, {replica_id})
    -- Waiting for the moment when the WAL write attempt counter
    -- on the replica increases.
    g.replica:exec(function(wal_cnt)
        t.helpers.retrying(
            {timeout=50},
            function()
                t.assert_gt(
                    box.error.injection.get('ERRINJ_WAL_WRITE_COUNT'),wal_cnt
                )
        end)
    end, {wal_cnt})
    -- Expecting memory leak detection by asan on replica stop.
    g.replica:stop()
end
