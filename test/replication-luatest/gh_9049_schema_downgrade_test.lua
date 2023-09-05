local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_test('test_schema_downgrade', function(cg)
    cg.master = server:new({alias = 'master'})
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            read_only = true,
            replication = cg.master.net_box_uri,
        },
    })
    cg.master:start()
    cg.replica:start()
end)

g.test_schema_downgrade = function(cg)
    local version = cg.master:exec(function()
        box.schema.downgrade('2.8.4')
        return box.space._schema:get('version')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function(version)
        t.assert(box.info.replication[1].upstream)
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        t.assert_equals(box.space._schema:get('version'), version)
    end, {version})
    cg.replica:restart()
    cg.replica:exec(function(version)
        t.assert(box.info.replication[1].upstream)
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        t.assert_equals(box.space._schema:get('version'), version)
    end, {version})
end

g.after_test('test_schema_downgrade', function(cg)
    cg.replica:drop()
    cg.master:drop()
end)
