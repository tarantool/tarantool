local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('gh_6123', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.cluster = cluster:new({})

    local box_cfg = {
        replication         = {
            server.build_listen_uri('master', cg.cluster.id)
        },
        replication_timeout = 1,
        read_only           = false
    }

    cg.master = cg.cluster:build_server({alias = 'master', box_cfg = box_cfg})

    local box_cfg = {
        replication         = {
            cg.master.net_box_uri,
            server.build_listen_uri('replica', cg.cluster.id)
        },
        replication_timeout = 1,
        replication_connect_timeout = 4,
        read_only           = true
    }

    cg.replica = cg.cluster:build_server({alias = 'replica', box_cfg = box_cfg})

    cg.cluster:add_server(cg.master)
    cg.cluster:add_server(cg.replica)
    cg.cluster:start()
end)


g.after_each(function(cg)
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
    cg.replica:wait_for_vclock_of(cg.master)

    t.assert_equals(cg.replica:eval("return box.space._schema:select{'smth'}"), {{'smth'}})
    t.assert_equals(cg.replica:eval("return box.info.replication[1].upstream.status"), 'follow')
end
