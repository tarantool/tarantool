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
            {'json1', require('json').decode('{"x":1}'), 10, {'json'},
                {data = {valid = true}}},
            box.tuple.new({'user1', 'test', 10, {'a', 'b'}, {author = 'test'}}),
            box.tuple.new({'user2', 3.14, 0, {}, {source = 'test'}}),
            box.tuple.new({'user3', true, 5, {'x'}, {ok = true}}),
            box.tuple.new({'user4', {nested = 'table'}, 100, {'tag1', 'tag2'},
                {meta = {level = 1}}}),
            box.tuple.new({'user5', nil, 1, {}, setmetatable({},
                {__serialize = 'map'})}),
            box.tuple.new({'user6', {'text', 42}, 7, {'tag'},
                {extra = 'data'}}),
            box.tuple.new({'user7', box.NULL, 2, {}, {empty = true}}),
            box.tuple.new({'user8', 'hello', 999, {'a', 'b'}, {count = 2}}),
            box.tuple.new({'user9', false, 8, {}, {flag = false}}),
            box.tuple.new({'user10', {'key', 'val'}, 3, {'key', 'val'},
                {dict = {k = 'v'}}}),
        }

        for _, case in ipairs(valid_cases) do
            local _, err = format:validate(case)
            if err then
                error("validation failed: " .. tostring(err) ..
                " for tuple: " .. require('json').encode(case))
            end
        end

        local invalid_cases = {
            {
                value = {1, 'value', 1, {}, {}},
                field = 1, name = 'key', expected = 'string',
                actual = 'unsigned',
            },
            {
                value = {'key', 'value', -1, {}, {}},
                field = 3, name = 'count', expected = 'unsigned',
                actual = 'integer',
            },
            {
                value = {'key', 'value', 1, {}, {}},
                field = 5, name = 'metadata', expected = 'map',
                actual = 'array',
            },
        }

        for _, case in ipairs(invalid_cases) do
            local message = string.format(
                    "Tuple field %d (%s) type does not match one required " ..
                    "by operation: expected %s, got %s",
                    case.field, case.name, case.expected, case.actual
                )
            local code = box.error.FIELD_TYPE

            local expected_error = {
                type = 'ClientError',
                code = code,
                message = message,
            }

            t.assert_error_covers(expected_error, format.validate, format,
                case.value)
        end
    end)
end
