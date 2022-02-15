local t = require('luatest')
local cluster = require('test.luatest_helpers.cluster')
local server = require('test.luatest_helpers.server')

local g = t.group('gh_6123', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    local engine = cg.params.engine

    cg.cluster = cluster:new({})

    local box_cfg = {
        replication         = {
            server.build_instance_uri('master')
        },
        replication_timeout = 1,
        read_only           = false
    }

    cg.master = cg.cluster:build_server({alias = 'master', engine = engine, box_cfg = box_cfg})

    local box_cfg = {
        replication         = {
            server.build_instance_uri('master'),
            server.build_instance_uri('replica')
        },
        replication_timeout = 1,
        replication_connect_timeout = 4,
        read_only           = true
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


g.test_truncate_is_local_transaction = function(cg)
    cg.master:eval("s = box.schema.space.create('temp', {temporary = true})")
    cg.master:eval("s:create_index('pk')")

    cg.master:eval("s:insert{1, 2}")
    cg.master:eval("s:insert{4}")
    t.assert_equals(cg.master:eval("return s:select()"), {{1, 2}, {4}})

    cg.master:eval("box.begin() box.space._schema:replace{'smth'} s:truncate() box.commit()")
    t.assert_equals(cg.master:eval("return s:select()"), {})
    t.assert_equals(cg.master:eval("return box.space._schema:select{'smth'}"), {{'smth'}})

    -- Checking that replica has received the last transaction,
    -- and that replication isn't broken.
    local vclock = cg.master:get_vclock()
    vclock[0] = nil
    cg.replica:wait_vclock(vclock)

    t.assert_equals(cg.replica:eval("return box.space._schema:select{'smth'}"), {{'smth'}})
    t.assert_equals(cg.replica:eval("return box.info.replication[1].upstream.status"), 'follow')
end
