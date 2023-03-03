local server = require('luatest.server')
local t = require('luatest')

local g = t.group("Upgrade to 3.0.0")

g.before_all(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/2.11.0',
    })
    cg.server:start()
    cg.server:exec(function()
        t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 0})
        box.schema.upgrade()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_new_replicaset_uuid_key = function(cg)
    cg.server:exec(function()
        local _schema = box.space._schema
        t.assert_equals(_schema:get{'cluster'}, nil)
        t.assert_equals(_schema:get{'replicaset_uuid'}.value,
                        box.info.replicaset.uuid)
    end)
end
