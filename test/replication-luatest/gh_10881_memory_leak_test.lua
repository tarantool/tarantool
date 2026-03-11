local server = require("luatest.server")
local cluster = require("luatest.replica_set")
local t = require("luatest")

local g = t.group('gh_asan_leak_on_shutdown')

g.before_each(function(g)
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
    box_cfg.read_only=true
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

    g.replica:exec(function() 
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
    end)

    local replica_id = g.replica:get_instance_id()

    g.master:exec(function(replica_id)
        box.space._cluster:delete{replica_id}
    end, {replica_id})

    g.replica:exec(function(wal_cnt) 
        t.helpers.retrying({timeout=50}, function() t.assert_gt(box.error.injection.get('ERRINJ_WAL_WRITE_COUNT') ,wal_cnt) end)
    end, {wal_cnt})

end

