local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all = function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end

g.after_all = function()
    g.server:stop()
end

g.test_drop_methods = function()
    g.server:exec(function()
        local t = require('luatest')

        local body = "function(x) return true end"
        box.schema.func.create('ck1', {is_deterministic = true, body = body})
        local func_id = box.func.ck1.id

        local fk0 = {one = {field = {a = 'a'}}, two = {field = {b = 'b'}}}
        local ck0 = {three = 'ck1', four = 'ck1'}
        local fk1 = {five = {field = 'a'}, six = {field = 'b'}}
        local ck1 = {seven = 'ck1', eight = 'ck1'}

        local fmt = {{'a', 'integer'}, {'b', 'integer'}}
        fmt[1].constraint = ck1
        fmt[2].foreign_key = fk1

        local def = {format = fmt, foreign_key = fk0, constraint = ck0}
        local s = box.schema.space.create('a', def)
        ck0.three = func_id
        ck0.four = func_id
        ck1.seven = func_id
        ck1.eight = func_id
        t.assert_equals(s.foreign_key, fk0)
        t.assert_equals(s.constraint, ck0)
        t.assert_equals(s:format()[1].constraint, ck1)
        t.assert_equals(s:format()[2].foreign_key, fk1)

        local res = [[Constraint 'one' does not exist in space 'a']]
        t.assert_error_msg_content_equals(res,
            function() s:drop_constraint('one') end)
        t.assert_error_msg_content_equals(res,
            function() s:drop_constraint('one', 'a') end)
        t.assert_error_msg_content_equals(res,
            function() s:drop_constraint('one', 1) end)

        s:drop_foreign_key('one')
        t.assert_equals(s.foreign_key, {two = {field = {b = 'b'}}})
        t.assert_equals(s.constraint, ck0)
        t.assert_equals(s:format()[1].constraint, ck1)
        t.assert_equals(s:format()[2].foreign_key, fk1)

        res = [[Foreign key 'one' does not exist in space 'a']]
        t.assert_error_msg_content_equals(res,
            function() s:drop_foreign_key('one') end)
        t.assert_error_msg_content_equals(res,
            function() s:drop_foreign_key('one', 'a') end)
        t.assert_error_msg_content_equals(res,
            function() s:drop_foreign_key('one', 1) end)

        s:drop_constraint('four')
        t.assert_equals(s.foreign_key, {two = {field = {b = 'b'}}})
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, ck1)
        t.assert_equals(s:format()[2].foreign_key, fk1)

        s:drop_constraint('seven', 'a')
        t.assert_equals(s.foreign_key, {two = {field = {b = 'b'}}})
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, {eight = func_id})
        t.assert_equals(s:format()[2].foreign_key, fk1)

        s:drop_foreign_key('two')
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, {eight = func_id})
        t.assert_equals(s:format()[2].foreign_key, fk1)

        s:drop_foreign_key('five', 'b')
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, {eight = func_id})
        t.assert_equals(s:format()[2].foreign_key, {six = {field = 'b'}})

        s:drop_constraint('eight', 1)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, nil)
        t.assert_equals(s:format()[2].foreign_key, {six = {field = 'b'}})

        s:drop_constraint('three')
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, nil)
        t.assert_equals(s:format()[1].constraint, nil)
        t.assert_equals(s:format()[2].foreign_key, {six = {field = 'b'}})

        s:drop_foreign_key('six', 2)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, nil)
        t.assert_equals(s:format()[1].constraint, nil)
        t.assert_equals(s:format()[2].foreign_key, nil)

        s:drop()
        box.schema.func.drop('ck1')
    end)
end

g.test_create_methods = function()
    g.server:exec(function()
        local t = require('luatest')

        local body = "function(x) return true end"
        box.schema.func.create('ck1', {is_deterministic = true, body = body})
        local func_id = box.func.ck1.id

        local s = box.schema.space.create('a', {format = {'a', 'b', 'c'}})
        local s1 = box.schema.space.create('b', {format = {'d', 'e', 'f'}})
        s:create_foreign_key('one', s1.name, {a = 'd', c = 'e', b = 'f'})
        local res = {one = {field = {a = 'd', c = 'e', b = 'f'}, space = s1.id}}
        t.assert_equals(s.foreign_key, res)
        s:create_foreign_key('two', s1.id, {a = 'd', c = 'e', b = 'f'})
        res['two'] = {field = {a = 'd', c = 'e', b = 'f'}, space = s1.id}
        t.assert_equals(s.foreign_key, res)
        s:create_foreign_key('three', nil, {a = 'c', c = 'b', b = 'a'})
        res['three'] = {field = {a = 'c', c = 'b', b = 'a'}, space = s.id}
        t.assert_equals(s.foreign_key, res)
        s:drop_foreign_key('one')
        s:drop_foreign_key('two')
        s:drop_foreign_key('three')
        t.assert_equals(s.foreign_key, nil)

        s:create_constraint('four', 'ck1')
        res = {four = func_id}
        t.assert_equals(s.constraint, res)
        s:create_constraint('ck1')
        res['ck1'] = func_id
        t.assert_equals(s.constraint, res)
        s:drop_constraint('four')
        s:drop_constraint('ck1')
        t.assert_equals(s.constraint, nil)

        s:create_foreign_key('five', s1.name, 'd', 'a')
        res = {five = {field = 1, space = s1.id}}
        t.assert_equals(s:format()[1].foreign_key, res)
        s:create_foreign_key('six', s1.id, 'e', 'a')
        res['six'] = {field = 2, space = s1.id}
        t.assert_equals(s:format()[1].foreign_key, res)
        s:create_foreign_key('seven', nil, 'c', 'a')
        res['seven'] = {field = 3, space = s.id}
        t.assert_equals(s:format()[1].foreign_key, res)
        s:drop_foreign_key('five', 'a')
        s:drop_foreign_key('six', 'a')
        s:drop_foreign_key('seven', 'a')
        t.assert_equals(s:format()[1].foreign_key, nil)

        s:create_constraint('eight', 'ck1', 'a')
        res = {eight = func_id}
        t.assert_equals(s:format()[1].constraint, res)
        s:create_constraint('ck1', nil, 'a')
        res['ck1'] = func_id
        t.assert_equals(s:format()[1].constraint, res)
        s:drop_constraint('eight', 'a')
        s:drop_constraint('ck1', 'a')
        t.assert_equals(s:format()[1].constraint, nil)

        -- Make sure empty fields are updated correctly.
        local v = box.space._space:get({s.id})
        v = v:totable()
        v[6].constraint = setmetatable({}, {__serialize = 'map'})
        v[6].foreign_key = setmetatable({}, {__serialize = 'map'})
        v[7][1].constraint = setmetatable({}, {__serialize = 'map'})
        v[7][2].foreign_key = setmetatable({}, {__serialize = 'map'})
        box.space._space:replace(v)
        s = box.space[s.id]
        t.assert_equals(s.constraint, nil)
        t.assert_equals(s:format()[1].constraint, {})
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s:format()[2].foreign_key, {})
        s:create_constraint('nine', 'ck1')
        s:create_constraint('ten', 'ck1', 'a')
        s:create_foreign_key('eleven', nil, {a = 'c'})
        s:create_foreign_key('twelve', nil, 'c', 'b')
        t.assert_equals(s.constraint, {nine = func_id})
        t.assert_equals(s:format()[1].constraint, {ten = func_id})
        t.assert_equals(s.foreign_key,
                        {eleven = {field = {a = 'c'}, space = s.id}})
        t.assert_equals(s:format()[2].foreign_key,
                        {twelve = {field = 3, space = s.id}})

        s:drop()
        s1:drop()
        box.schema.func.drop('ck1')
    end)
end
