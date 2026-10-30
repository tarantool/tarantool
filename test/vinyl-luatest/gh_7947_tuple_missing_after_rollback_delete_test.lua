local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        alias = 'master',
        box_cfg = {
            vinyl_cache = 1024 * 1024,
        },
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_tuple_missing_after_rollback_delete = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
        s:insert({1, 1})
        s:insert({1, 2})
        s:insert({1, 3})
        box.begin()
        box.space.test:delete({1, 2})
        t.assert_equals(s:select({1}), {{1, 1}, {1, 3}})
        box.rollback()
        t.assert_equals(s:select({1}), {{1, 1}, {1, 2}, {1, 3}})
        s:drop()
    end)
end
