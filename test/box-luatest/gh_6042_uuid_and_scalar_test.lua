local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'uuid_scalar'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_uuid_scalar = function()
    g.server:exec(function()
        local s = box.schema.space.create('a', {format = {{'u', 'uuid'}}})
        local _ = s:create_index('i', {parts = {{'u', 'scalar'}}})
        t.assert_equals(s:format()[1].type, 'uuid')
        t.assert_equals(s.index[0].parts[1].type, 'scalar')
        s:drop()

        s = box.schema.space.create('a', {format = {{'u', 'scalar'}}})
        _ = s:create_index('i', {parts = {{'u', 'uuid'}}})
        t.assert_equals(s:format()[1].type, 'scalar')
        t.assert_equals(s.index[0].parts[1].type, 'uuid')
        s:drop()
    end)
end
