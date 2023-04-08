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

-- Checks that `box.tuple.format.new` works as expected.
g.test_box_tuple_format_new = function()
    t.assert_equals(type(box.tuple.format.new{}), 'userdata')
    t.assert(box.tuple.format.is(box.tuple.format.new{}))
    t.assert_not(box.tuple.format.is(777))

    local err_msg = "Illegal parameters, format should be a table"
    t.assert_error_msg_content_equals(err_msg, function ()
        box.tuple.format.new(777)
    end)
    err_msg = "Wrong space format field 1: unknown field type"
    t.assert_error_msg_content_equals(err_msg, function ()
        box.tuple.format.new{{'field', 'unknown'}}
    end)
    err_msg = "unsupported Lua type 'function'"
    t.assert_error_msg_content_equals(err_msg, function ()
        box.tuple.format.new{{'field', 'number',
                              nullable_action = function() end}}
    end)
    err_msg = "Space field 'field' is duplicate"
    t.assert_error_msg_content_equals(err_msg, function ()
        box.tuple.format.new{{'field'}, {'field'}}
    end)
end

g.before_test('test_box_tuple_format_gc', function (cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_TUPLE_FORMAT_COUNT', 2)
    end)
end)

-- Checks that tuple formats are garbage collected and recycled.
g.test_box_tuple_format_gc = function(cg)
    t.tarantool.skip_if_not_debug()

    cg.server:exec(function()
        box.tuple.format.new{{name = 'field0'}}
        collectgarbage()
        box.tuple.format.new{{name = 'field1'}}
    end)
end

g.after_test('test_box_tuple_format_gc', function (cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_TUPLE_FORMAT_COUNT', -1)
    end)
end)


-- Checks that box.tuple.format serialization works as expected.
g.test_box_tuple_format_serialization = function(cg)
    cg.server:exec(function()
        local err_msg = "Illegal parameters, format should be a table"
        t.assert_error_msg_content_equals(err_msg, function()
            box.tuple.format.new()
        end)
        local f = box.tuple.format.new{}
        t.assert_equals(tostring(f), "box.tuple.format")
        local mt = getmetatable(f)
        t.assert_equals(mt.__serialize, mt.totable)
        t.assert_equals(mt.ipairs, mt.pairs)

        err_msg = "box.tuple.format expected, got no value"
        t.assert_error_msg_contains(err_msg, function()
            f.__tostring()
        end)
        t.assert_error_msg_contains(err_msg, function()
            f.totable()
        end)
        t.assert_error_msg_contains(err_msg, function()
            f.pairs()
        end)

        local test_tuple_format_contents = function(f, expected)
            local actual = {}
            for i, field in f:pairs() do
                table.insert(actual, i, field)
            end
            t.assert_equals(actual, expected)
            t.assert_equals(f:totable(), expected)
        end

        f = box.tuple.format.new{}
        test_tuple_format_contents(f, {})

        local f = box.tuple.format.new{}
        test_tuple_format_contents(f, {})

        f = box.tuple.format.new{{name = 'field1', type = 'string'},
                                 {name = 'field2', type = 'number'}}
        test_tuple_format_contents(f, {{name = 'field1', type = 'string'},
                                       {name = 'field2', type = 'number'}})

        local contents = {{name = 'field', type = 'string'}}
        f = box.tuple.format.new { { 'field', 'string' } }
        test_tuple_format_contents(f, contents)
        f = box.tuple.format.new{{'field', type = 'string'}}
        test_tuple_format_contents(f, contents)
        f = box.tuple.format.new{{name = 'field', 'string'}}
        test_tuple_format_contents(f, contents)
        f = box.tuple.format.new{{name = 'field', type = 'string'}}
        test_tuple_format_contents(f, contents)
        contents = {{name = 'field1', type = 'string', is_nullable = true,
                     nullable_action = 'none', collation = 'unicode_uk_s2',
                     default = 'UPPER("string")',
                     constraint = {ck = 'box.schema.user.info'},
                     foreign_key = {fk = {space = '_space', field = 'name'}}},
                     {name = 'field2', type = 'any', nullable_action = 'ignore',
                      foreign_key = {fk = {space = '_space', field = 1}}}}
        f = box.tuple.format.new(contents)
        contents[1].collation =
            box.space._collation.index.name:get{contents[1].collation}.id
        contents[1].constraint =
            {ck = box.space._func.index.name:get{contents[1].constraint.ck}.id}
        local sid =
            box.space._space.index.name:get{contents[1].foreign_key.fk.space}.id
        contents[1].foreign_key.fk.space = sid
        contents[2].foreign_key.fk.space = sid
        test_tuple_format_contents(f, contents)
        local fk = {field = 'name'}
        contents =  {{name = 'field1', type = 'any', foreign_key = fk}}
        f = box.tuple.format.new(contents)
        contents[1].foreign_key = {unknown = fk}
        test_tuple_format_contents(f, contents)
    end)
end

-- Checks that `box.tuple.new` with format option works as expected.
g.test_box_tuple_new_with_format = function(cg)
    cg.server:exec(function()
        local f = {{name = 'field', type = 'number'}}
        local options = {format = f}
        local tuple = box.tuple.new({1}, options)
        -- Checks that options table is not changed by box.tuple.new.
        t.assert_equals(options, {format = f})
        t.assert_equals(tuple:tomap{names_only = true}, {field = 1})
        tuple = box.tuple.new(1, options)
        t.assert_equals(tuple:tomap{names_only = true}, {field = 1})
        tuple = box.tuple.new(box.tuple.new(1, options), options)
        t.assert_equals(tuple:tomap{names_only = true}, {field = 1})
        tuple = box.tuple.new({1, 'str'}, {format = f})
        t.assert_equals(tuple:tomap{names_only = true}, {field = 1})
        local err_msg = "Tuple field 1 (field) type does not match one " ..
                        "required by operation: expected number, got string"
        t.assert_error_msg_content_equals(err_msg, function()
            box.tuple.new({'str'}, {format = f})
        end)
        err_msg = "Tuple field 1 (field) required by space format is missing"
        t.assert_error_msg_content_equals(err_msg, function()
            box.tuple.new({}, {format = f})
        end)
        f = {{name = 'field', type = 'number', is_nullable = true}}
        tuple = box.tuple.new({}, {format = f})
        t.assert_equals(tuple:tomap{names_only = true}, {})
        f = box.tuple.format.new{{'field', 'number'}}
        tuple = box.tuple.new({1}, {format = f})
        t.assert_equals(tuple:tomap{names_only = true}, {field = 1})
        tuple = box.tuple.new({1, 'str'}, {format = f})
        t.assert_equals(tuple:tomap{names_only = true}, {field = 1})
        err_msg = "Illegal parameters, options should be a table"
        t.assert_error_msg_content_equals(err_msg, function()
            box.tuple.new({'str'}, 'fmt')
        end)
        err_msg = "Illegal parameters, format should be a table"
        t.assert_error_msg_content_equals(err_msg, function()
            box.tuple.new({'str'}, {})
        end)

        -- Checks that check constraint is disabled.
        box.schema.func.create('ck', {is_deterministic = true,
                                      body = "function () return false end"})
        box.tuple.new({0}, {format = {{'field', constraint = 'ck'}}})
        box.func.ck:drop()

        -- Checks that foreign key constraint is disabled.
        box.schema.space.create('s')
        box.tuple.new({0}, {format =
            {{'field', foreign_key = {space = 's', field = 1}}}})
        box.space.s:drop()
        box.tuple.format.new{{'field', foreign_key = {field = 1}}}
        box.tuple.format.new{{'field', foreign_key = {fk = {field = 1}}}}
        err_msg = 'Illegal parameters, format[1]: foreign key: space ' ..
                  'nonexistent was not found'
        t.assert_error_msg_content_equals(err_msg, function()
            box.tuple.format.new{{'field', foreign_key = {space = 'nonexistent',
                                                          field = 1}}}
        end)
    end)
end

-- Checks that `box.tuple.new` with scalar argument works correctly.
g.test_tuple_new_with_scalar_argument = function()
    t.assert_equals(box.tuple.new(1):totable(), {1})
    t.assert_equals(box.tuple.new(box.tuple.new(1)):totable(), {1})
end

-- Checks that `box.tuple.new` backward compatibility works correctly.
g.test_box_tuple_new_with_compat = function(cg)
    cg.server:exec(function()
        local compat = require('compat')

        local err_msg = 'Illegal parameters, options should be a table'
        t.assert_error_msg_content_equals(err_msg, function()
            box.tuple.new(1, 2)
        end)
        compat.box_tuple_new_vararg = 'old'
        t.assert_equals(box.tuple.new(1, 2, 3):totable(), {1, 2, 3})
        local fmt_table = {{'field', 'number'}}
        local fmt = box.tuple.format.new(fmt_table)
        local tuple = box.tuple.new({1}, {format = fmt})
        t.assert_equals(tuple:tomap{names_only = true}, {})
        tuple = box.tuple.new(box.tuple.new{'str'}, {format = fmt_table})
        t.assert_equals(tuple:tomap{names_only = true}, {})
    end)
end
