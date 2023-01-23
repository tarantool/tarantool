local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

--
-- Checks that accessing a tuple field by [*] ('any' token) returns nothing
-- (gh-5226).
--
g.test_tuple_access_by_any_token = function()
    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('sk', {parts = {{'[2][1][1]', 'unsigned'}}})
        local tuple = s:replace({1, {{1, 2}}, {{3, 4}}})
        t.assert_equals(tuple['[*]'], nil)
        t.assert_equals(tuple['[*][1]'], nil)
        t.assert_equals(tuple['[1]'], 1)
        t.assert_equals(tuple['[1][*]'], nil)
        t.assert_equals(tuple['[2]'], {{1, 2}})
        t.assert_equals(tuple['[2][*]'], nil)
        t.assert_equals(tuple['[2][*][1]'], nil)
        t.assert_equals(tuple['[2][1]'], {1, 2})
        t.assert_equals(tuple['[2][1][*]'], nil)
        t.assert_equals(tuple['[2][1][1]'], 1)
        t.assert_equals(tuple['[2][1][2]'], 2)
        t.assert_equals(tuple['[2][1][2][*]'], nil)
        t.assert_equals(tuple['[3]'], {{3, 4}})
        t.assert_equals(tuple['[3][*]'], nil)
        t.assert_equals(tuple['[3][*][1]'], nil)
        t.assert_equals(tuple['[3][1]'], {3, 4})
        t.assert_equals(tuple['[3][1][*]'], nil)
        t.assert_equals(tuple['[3][1][1]'], 3)
        t.assert_equals(tuple['[3][1][2]'], 4)
        t.assert_equals(tuple['[3][1][2][*]'], nil)
        s:drop()
    end)
end
