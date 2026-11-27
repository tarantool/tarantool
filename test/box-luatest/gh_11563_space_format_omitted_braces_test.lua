local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_reject_non_array_formats = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {
            format = {{name = 'id', type = 'unsigned'},
                      {name = 'value', type = 'string'}},
        })
        s:create_index('pk')
        s:insert({1, 'value'})
        local before = s:format()
        local cases = {
            {name = 'a', type = 'unsigned'},
            {'a', type = 'unsigned'},
            {name = 'a', [1] = 'unsigned'},
            {{name = 'a'}, type = 'unsigned'},
            {'a', extra = true},
            {[2] = {name = 'a'}},
            {[1] = {name = 'a'}, [3] = {name = 'b'}},
            {[0] = {name = 'a'}, [1] = {name = 'b'}},
            {[-1] = {name = 'a'}, [1] = {name = 'b'}},
            {[1] = {name = 'a'}, [1.5] = {name = 'b'}},
            {[true] = {name = 'a'}},
        }
        for _, format in ipairs(cases) do
            t.assert_error_msg_contains('format must be an array',
                                       s.format, s, format)
            t.assert_equals(s:format(), before)
            t.assert_equals(s:get({1}), {1, 'value'})

            t.assert_error_msg_contains('format must be an array',
                                       s.alter, s, {format = format})
            t.assert_equals(s:format(), before)
            t.assert_equals(s:get({1}), {1, 'value'})

            t.assert_error_msg_contains('format must be an array',
                                       box.schema.space.create, 'invalid',
                                       {format = format})
            t.assert_equals(box.space.invalid, nil)

            t.assert_error_msg_contains('format must be an array',
                                       box.tuple.format.new, format)
        end
    end)
end

g.test_array_error_details = function(cg)
    cg.server:exec(function()
        local cases = {
            {
                format = {name = 'a'},
                err = 'An array contains a non-numeric key: "name"',
            },
            {
                format = {[1.5] = 'a'},
                err = 'An array contains a non-integral numeric key: 1.5',
            },
            {
                format = {[2] = 'a'},
                err = 'An array must start from index 1, got min index 2',
            },
            {
                format = {[1] = 'a', [3] = 'b'},
                err = 'An array must not have holes, got a table with 2 ' ..
                      'integer fields with max index 3',
            },
        }
        for _, case in ipairs(cases) do
            t.assert_error_msg_equals('format must be an array: ' .. case.err,
                                     box.tuple.format.new, case.format)
        end
    end)
end

g.test_valid_array_formats = function(cg)
    cg.server:exec(function()
        local cases = {
            {},
            {'a'},
            {'a', 'b'},
            {{'a', 'unsigned'}},
            {{name = 'a', type = 'unsigned', is_nullable = true}},
            {{'a', type = 'unsigned'}},
            {{name = 'a', [1] = 'unsigned'}},
            {'a', {'b', 'string'}},
        }
        for _, format in ipairs(cases) do
            local expected = box.tuple.format.new(format):totable()
            local s = box.schema.space.create('test', {format = format})
            t.assert_equals(s:format(), expected)
            s:format({})
            s:format(format)
            t.assert_equals(s:format(), expected)
            s:alter({format = {}})
            s:alter({format = format})
            t.assert_equals(s:format(), expected)
            s:drop()
        end

        -- Pure arrays of names must not become positional field definitions.
        t.assert_equals(box.tuple.format.new({'a', 'unsigned'}):totable(), {
            {name = 'a', type = 'any'},
            {name = 'unsigned', type = 'any'},
        })
    end)
end

g.test_empty_array_clears_format = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {
            format = {{name = 'a', type = 'unsigned'}},
        })
        s:format({})
        t.assert_equals(s:format(), {})

        s:format({{'a', 'unsigned'}})
        s:alter({format = {}})
        t.assert_equals(s:format(), {})
        t.assert_equals(box.tuple.format.new({}):totable(), {})
    end)
end

g.test_field_definition_validation = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {
            format = {{name = 'a', type = 'unsigned'}},
        })
        local before = s:format()
        t.assert_error_msg_contains('format[1]: name (string) is expected',
                                   s.format, s, {{type = 'unsigned'}})
        t.assert_error_msg_contains('format[1]: type must be a string',
                                   s.format, s, {{name = 'a', type = 1}})
        t.assert_equals(s:format(), before)
    end)
end
