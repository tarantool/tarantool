local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'gh-5678'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_join_with_unsupported_index = function()
    g.server:exec(function()
        local s = box.schema.space.create('T', {format = {'I'}})
        s:create_index('ii', {type = 'hash'})
        local _, err = box.execute([[SELECT a.i FROM t AS a, t;]])
        local msg = [[SQL does not support using non-TREE index type. ]]..
            [[Please, use INDEXED BY clause to force using proper index.]]
        t.assert_equals(err.message, msg)
        s:drop()
    end)
end
