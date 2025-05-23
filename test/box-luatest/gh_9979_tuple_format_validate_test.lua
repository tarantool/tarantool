local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.after_each(function()
    g.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_tuple_format_validate = function()
    g.server:exec(function()
        local format = box.tuple.format.new({
            {name = 'key', type = 'string'},
            {name = 'value', type = 'any'},
            {name = 'count', type = 'unsigned'},
            {name = 'tags', type = 'array'},
            {name = 'metadata', type = 'map'},
        })

        local valid_cases = {
            {'key1', 'value1', 1, {'tag1', 'tag2'}, {author = 'user'}},
            {'key2', 140, 0, {}, setmetatable({}, {__serialize = 'map'})},
            {'key3', 0.5, 100, {1, 2, 3}, {info = {value = 1}}},
            {'key4', true, 42, {'a'}, {x = 1, y = 2}},
            {'id1', nil, 5, {'tagA', 'tagB'}, {created_by = 'system'}},
            {'item1', {sub = 'obj'}, 10, {}, {role = 'admin', active = true}},
            {'abc', 3.14, 7, {'pi'}, {math = 'pi', valid = true}},
            {'name', {}, 2, {'test'}, {lang = 'en'}},
            {'key5', box.NULL, 0, {}, {status = 'none'}},
            {'k1', 'v1', 999999, {}, {size = 'big'}},
            {'key6', {nested = {more = {levels = true}}}, 1, {'tag'},
                {structure = 'ok'}},
            {'t1', 'b1', 123, {'x', 'y', 'z'}, {list = {1, 2, 3}, note = 'yes'}},
            {'json1', {x = 1}, 10, {'json'}, {data = {valid = true}}},
        }

        for _, case in ipairs(valid_cases) do
            format:validate(case)
            local tuple_case = box.tuple.new(case)
            format:validate(tuple_case)
        end

        local invalid_cases = {
            {
                -- key not a string
                value = {1, 'value', 1, {}, {}},
                field = 1, name = 'key', expected = 'string',
                actual = 'unsigned',
            },
            {
                -- count negative
                value = {'key', 'value', -1, {}, {}},
                field = 3, name = 'count', expected = 'unsigned',
                actual = 'integer',
            },
            {
                -- metadata not a map
                value = {'key', 'value', 1, {}, {}},
                field = 5, name = 'metadata', expected = 'map',
                actual = 'array',
            },
            {
                -- tags not an array
                value = {'key', 'value', 1, 'not_array', {}},
                field = 4, name = 'tags',
                expected = 'array', actual = 'string',
            },
        }

        for _, case in ipairs(invalid_cases) do
            local message = string.format(
                "Tuple field %d (%s) type does not match one required " ..
                "by operation: expected %s, got %s",
                case.field, case.name, case.expected, case.actual
            )

            t.assert_error_covers({
                type = 'ClientError',
                code = box.error.FIELD_TYPE,
                message = message,
            },
            format.validate, format, case.value)

            local tuple_case = box.tuple.new(case.value)
            t.assert_error_covers({
                type = 'ClientError',
                code = box.error.FIELD_TYPE,
                message = message,
            },
            format.validate, format, tuple_case)
        end
    end)
end

g.test_space_format_object = function()
    g.server:exec(function()
        local t = require('luatest')
        local s = box.schema.space.create('test', {
            format = {
                {name = 'id', type = 'string'},
                {name = 'data', type = 'any'},
            }
        })
        t.assert(s.format_object ~= nil, 'space.format_object should exist')
        -- it must be a box.tuple.format
        t.assert(box.tuple.format.is(s.format_object),
                 'format_object must be a box.tuple.format')

        -- positive: valid tuple passes
        s.format_object:validate({'foo', 123})

        -- negative: wrong id type raises FIELD_TYPE
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.FIELD_TYPE,
        }, s.format_object.validate, s.format_object, {1, 123})
    end)
end

g.test_format_validate_usage_error = function()
    g.server:exec(function()
        local t = require('luatest')
        local format = box.tuple.format.new({{name = 'a', type = 'unsigned'}})

        -- invalid format objects (`self`)
        for _, bad_fmt in ipairs({123, 'str', true, box.NULL, {}}) do
            t.assert_error_msg_equals(
                string.format(
                    "bad argument #1 to '?' (box.tuple.format expected, got %s)",
                    type(bad_fmt)
                ), format.validate, bad_fmt, {1}
            )
        end

        -- no args
        t.assert_error_msg_equals(
            'Usage: format:validate(tuple_or_table)',
            format.validate, format
        )

        -- wrong arg types
        for _, bad in ipairs({123, 'str', true, box.NULL}) do
            t.assert_error_msg_equals(
                'Expected tuple or table',
                format.validate, format, bad
            )
        end
    end)
end
