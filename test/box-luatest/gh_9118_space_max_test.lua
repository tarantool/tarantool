local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        local compat = require('compat')
        compat.box_space_max = 'default'
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_space_max = function(cg)
    cg.server:exec(function()
        local compat = require('compat')
        t.assert_equals(compat.box_space_max.current, 'default')
        t.assert_equals(compat.box_space_max.default, 'new')
        t.assert_equals(box.schema.SPACE_MAX, 2147483646)
        t.assert_error_msg_equals(
            "Failed to create space 'test': space id is too big",
            box.schema.create_space, 'test', {id = 2147483647})
        local s = box.schema.create_space('test', {id = 2147483646})
        s:drop()
        compat.box_space_max = 'old'
        t.assert_error_msg_equals(
            "Failed to create space 'test': space id is too big",
            box.schema.create_space, 'test', {id = 2147483648})
        local s = box.schema.create_space('test', {id = 2147483647})
        s:drop()
        compat.box_space_max = 'new'
        t.assert_error_msg_equals(
            "Failed to create space 'test': space id is too big",
            box.schema.create_space, 'test', {id = 2147483647})
        local s = box.schema.create_space('test', {id = 2147483646})
        s:drop()
    end)
end
