local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_cursor_invalidation'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_cursor_invalidation = function()
    g.server:exec(function()
        local t = require('luatest')
        local s = box.schema.space.create('T', {format = {'A'}})
        s:create_index('ii')
        s:insert({1,2,3,4,5})
        s:insert({2})
        t.assert_equals(box.execute([[SELECT * FROM t;]]).rows, {{1}, {2}})
        s:drop()
    end)
end
