local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_test('test_schema_upgrade', function(cg)
    cg.master = server:new({
        alias = 'master',
        datadir = 'test/replication-luatest/upgrade/2.11.1/master',
    })
    cg.replica = server:new({
        alias = 'replica',
        datadir = 'test/replication-luatest/upgrade/2.11.1/replica',
        box_cfg = {
            read_only = true,
            replication = cg.master.net_box_uri,
        },
    })
    cg.master:start()
    cg.replica:start()
end)

g.test_schema_upgrade = function(cg)
    cg.replica:exec(function()
        t.assert_equals(box.space._schema:get('version'), {'version', 2, 11, 1})
        t.assert(box.internal.schema_needs_upgrade())
    end)
    local version = cg.master:exec(function()
        t.assert_equals(box.space._schema:get('version'), {'version', 2, 11, 1})
        t.assert(box.internal.schema_needs_upgrade())
        box.schema.upgrade()
        t.assert_not(box.internal.schema_needs_upgrade())
        return box.space._schema:get('version')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function(version)
        t.assert_not(box.internal.schema_needs_upgrade())
        t.assert_equals(box.space._schema:get('version'), version)
    end, {version})
end

g.after_test('test_schema_upgrade', function(cg)
    cg.replica:drop()
    cg.master:drop()
end)
