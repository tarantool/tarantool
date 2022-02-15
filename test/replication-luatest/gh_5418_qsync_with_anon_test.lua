local t = require('luatest')
local cluster = require('test.luatest_helpers.cluster')
local server = require('test.luatest_helpers.server')

local g = t.group('gh-5418', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    local engine = cg.params.engine

    cg.cluster = cluster:new({})

    local box_cfg = {
        replication         = {
            server.build_instance_uri('master')
        },
        replication_synchro_quorum = 2,
        replication_timeout = 1
    }

    cg.master = cg.cluster:build_server({alias = 'master', engine = engine, box_cfg = box_cfg})

    local box_cfg = {
        replication         = {
            server.build_instance_uri('master'),
            server.build_instance_uri('replica')
        },
        replication_timeout = 1,
        replication_connect_timeout = 4,
        read_only           = true,
        replication_anon    = true
    }

    cg.replica = cg.cluster:build_server({alias = 'replica', engine = engine, box_cfg = box_cfg})

    cg.cluster:add_server(cg.master)
    cg.cluster:add_server(cg.replica)
    cg.cluster:start()
end)


g.after_each(function(cg)
    cg.cluster.servers = nil
    cg.cluster:drop()
end)


g.test_qsync_with_anon = function(cg)
    cg.master:eval("box.schema.space.create('sync', {is_sync = true})")
    cg.master:eval("box.space.sync:create_index('pk')")
    cg.master:eval("box.ctl.promote()")

    t.assert_error_msg_content_equals("Quorum collection for a synchronous transaction is timed out",
        function() cg.master:eval("return box.space.sync:insert{1}") end)

    -- Wait until everything is replicated from the master to the replica
    local vclock = cg.master:eval("return box.info.vclock")
    vclock[0] = nil
    cg.replica:wait_vclock(vclock)

    t.assert_equals(cg.master:eval("return box.space.sync:select()"), {})
    t.assert_equals(cg.replica:eval("return box.space.sync:select()"), {})
end
