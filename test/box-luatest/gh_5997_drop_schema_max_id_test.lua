local server = require('luatest.server')
local t = require('luatest')

local g = t.group("Absence of _schema.max_id")

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_no_max_id = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.space._schema:get("max_id"), nil)
        local s = box.schema.create_space('test')
        t.assert_not_equals(s, nil)
        t.assert_equals(box.space._schema:get("max_id"), nil)
        s:drop()
    end)
end

local g = t.group("Upgrade")

g.before_each(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/2.11.0',
    })
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_upgrade = function(cg)
    cg.server:exec(function()
        t.assert_not_equals(box.space._schema:get("max_id"), nil)
        box.schema.upgrade()
        t.assert_equals(box.space._schema:get("max_id"), nil)
    end)
end
