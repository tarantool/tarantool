#!/usr/bin/env tarantool

package.path = "lua/?.lua;"..package.path

local tap = require('tap')
local common = require('serializer_test')

local function is_map(s)
    return string.sub(s, 1, 1) == "{"
end

local function is_array(s)
    return string.sub(s, 1, 1) == "["
end

local function test_misc(test, s)
    test:plan(2)
    test:iscdata(s.NULL, 'void *', '.NULL is cdata')
    test:ok(s.NULL == nil, '.NULL == nil')
end

local test = tap.test("json")

test:plan(1)

test:test("json", function(test)
    local serializer = require('json')
    test:plan(86)

    test:test("unsigned", common.test_unsigned, serializer)
    test:test("signed", common.test_signed, serializer)
    test:test("double", common.test_double, serializer)
    test:test("boolean", common.test_boolean, serializer)
    test:test("string", common.test_string, serializer)
    test:test("nil", common.test_nil, serializer)
    test:test("table", common.test_table, serializer, is_array, is_map)
    test:test("ucdata", common.test_ucdata, serializer)
    test:test("depth", common.test_depth, serializer)
    test:test("misc", test_misc, serializer)

    --
    -- gh-2888: Check the possibility of using options in encode()/decode().
    --
    local orig_encode_deep_as_nil = serializer.cfg.encode_deep_as_nil
    local orig_encode_max_depth = serializer.cfg.encode_max_depth
    local sub = {a = 1, { b = {c = 1, d = {e = 1}}}}
    serializer.cfg({encode_max_depth = 1, encode_deep_as_nil = true})
    test:ok(serializer.encode(sub) == '{"1":null,"a":1}',
            'depth of encoding is 1 with .cfg')
    serializer.cfg({encode_max_depth = orig_encode_max_depth})
    test:ok(serializer.encode(sub, {encode_max_depth = 1}) == '{"1":null,"a":1}',
            'depth of encoding is 1 with .encode')
    test:is(serializer.cfg.encode_max_depth, orig_encode_max_depth,
            'global option remains unchanged')

    local orig_encode_invalid_numbers = serializer.cfg.encode_invalid_numbers
    local nan = 1/0
    serializer.cfg({encode_invalid_numbers = false})
    test:ok(not pcall(serializer.encode, {a = nan}),
            'expected error with NaN encoding with .cfg')
    serializer.cfg({encode_invalid_numbers = orig_encode_invalid_numbers})
    test:ok(not pcall(serializer.encode, {a = nan},
                      {encode_invalid_numbers = false}),
            'expected error with NaN encoding with .encode')
    test:is(serializer.cfg.encode_invalid_numbers, orig_encode_invalid_numbers,
            'global option remains unchanged')

    local orig_encode_number_precision = serializer.cfg.encode_number_precision
    local number = 0.12345
    serializer.cfg({encode_number_precision = 3})
    test:ok(serializer.encode({a = number}) == '{"a":0.123}',
            'precision is 3')
    serializer.cfg({encode_number_precision = orig_encode_number_precision})
    test:ok(serializer.encode({a = number}, {encode_number_precision = 3}) ==
            '{"a":0.123}', 'precision is 3')
    test:is(serializer.cfg.encode_number_precision, orig_encode_number_precision,
            'global option remains unchanged')

    local orig_decode_invalid_numbers = serializer.cfg.decode_invalid_numbers
    serializer.cfg({decode_invalid_numbers = false})
    test:ok(not pcall(serializer.decode, '{"a":inf}'),
            'expected error with NaN decoding with .cfg')
    serializer.cfg({decode_invalid_numbers = orig_decode_invalid_numbers})
    test:ok(not pcall(serializer.decode, '{"a":inf}',
                      {decode_invalid_numbers = false}),
            'expected error with NaN decoding with .decode')
    test:is(serializer.cfg.decode_invalid_numbers, orig_decode_invalid_numbers,
            'global option remains unchanged')

    local orig_decode_max_depth = serializer.cfg.decode_max_depth
    serializer.cfg({decode_max_depth = 2})
    test:ok(not pcall(serializer.decode, '{"1":{"b":{"c":1,"d":null}},"a":1}'),
            'error: too many nested data structures')
    serializer.cfg({decode_max_depth = orig_decode_max_depth})
    test:ok(not pcall(serializer.decode, '{"1":{"b":{"c":1,"d":null}},"a":1}',
                      {decode_max_depth = 2}),
            'error: too many nested data structures')
    test:is(serializer.cfg.decode_max_depth, orig_decode_max_depth,
            'global option remains unchanged')

    --
    -- gh-3514: fix parsing integers with exponent in json
    --
    test:is(serializer.decode('{"var":2.0e+3}')["var"], 2000)
    test:is(serializer.decode('{"var":2.0e+3}')["var"], 2000)
    test:is(serializer.decode('{"var":2.0e+3}')["var"], 2000)
    test:is(serializer.decode('{"var":2.0e+3}')["var"], 2000)

    --
    -- gh-4366: segmentation fault with recursive table
    --
    serializer.cfg({encode_max_depth = 2})
    local rec1 = {}
    rec1[1] = rec1
    test:is(serializer.encode(rec1), '[[null]]')
    local rec2 = {}
    rec2['x'] = rec2
    test:is(serializer.encode(rec2), '{"x":{"x":null}}')
    local rec3 = {}
    rec3[1] = rec3
    rec3[2] = rec3
    test:is(serializer.encode(rec3), '[[null,null],[null,null]]')
    local rec4 = {}
    rec4['a'] = rec4
    rec4['b'] = rec4
    test:is(serializer.encode(rec4),
            '{"a":{"a":null,"b":null},"b":{"a":null,"b":null}}')
    serializer.cfg({encode_max_depth = orig_encode_max_depth,
                    encode_deep_as_nil = orig_encode_deep_as_nil})

    --
    -- gh-3316: Make sure that line number is printed in the error
    -- message.
    --
    local _, err_msg
    _, err_msg = pcall(serializer.decode, 'a{"hello": \n"world"}')
    test:ok(string.find(err_msg, 'line 1 at character 1') ~= nil,
            'mistake on first line')
    _, err_msg = pcall(serializer.decode, '{"hello": \n"world"a}')
    test:ok(string.find(err_msg, 'line 2 at character 8') ~= nil,
            'mistake on second line')
    _, err_msg = pcall(serializer.decode, '\n\n\n\n{"hello": "world"a}')
    test:ok(string.find(err_msg, 'line 5 at character 18') ~= nil,
            'mistake on fifth line')
    serializer.cfg{decode_max_depth = 1}
    _, err_msg = pcall(serializer.decode,
                       '{"hello": {"world": {"hello": "world"}}}')
    test:ok(string.find(err_msg, 'line 1 at character 11') ~= nil,
            'mistake on first line')
    _, err_msg = pcall(serializer.decode,
                       '{"hello": \n{"world": {"hello": "world"}}}')
    test:ok(string.find(err_msg, 'line 2 at character 1') ~= nil,
            'mistake on second line')
    _, err_msg = pcall(serializer.decode, "{ 100: 200 }")
    test:ok(string.find(err_msg, 'line 1 at character 3') ~= nil,
            'mistake on first line')
    _, err_msg = pcall(serializer.decode, '{"hello": "world",\n 100: 200}')
    test:ok(string.find(err_msg, 'line 2 at character 2') ~= nil,
            'mistake on second line')

    --
    -- gh-4339: Make sure that tokens 'T_*' are absent in error
    -- messages and a context is printed.
    --
    _, err_msg = pcall(serializer.decode, '{{: "world"}')
    test:ok(string.find(err_msg, '\'{\'') ~= nil, '"{" instead of T_OBJ_BEGIN')
    _, err_msg = pcall(serializer.decode, '{"a": "world"}}')
    test:ok(string.find(err_msg, '\'}\'') ~= nil, '"}" instead of T_OBJ_END')
    _, err_msg = pcall(serializer.decode, '{[: "world"}')
    test:ok(string.find(err_msg, '\'[\'', 1, true) ~= nil,
            '"[" instead of T_ARR_BEGIN')
    _, err_msg = pcall(serializer.decode, '{]: "world"}')
    test:ok(string.find(err_msg, '\']\'', 1, true) ~= nil,
            '"]" instead of T_ARR_END')
    _, err_msg = pcall(serializer.decode, '{1: "world"}')
    test:ok(string.find(err_msg, 'int') ~= nil, 'int instead of T_INT')
    _, err_msg = pcall(serializer.decode, '{1.0: "world"}')
    test:ok(string.find(err_msg, 'number') ~= nil, 'number instead of T_NUMBER')
    _, err_msg = pcall(serializer.decode, '{true: "world"}')
    test:ok(string.find(err_msg, 'boolean') ~= nil,
            'boolean instead of T_BOOLEAN')
    _, err_msg = pcall(serializer.decode, '{null: "world"}')
    test:ok(string.find(err_msg, 'null') ~= nil, 'null instead of T_NULL')
    _, err_msg = pcall(serializer.decode, '{:: "world"}')
    test:ok(string.find(err_msg, 'colon') ~= nil, 'colon instead of T_COLON')
    _, err_msg = pcall(serializer.decode, '{,: "world"}')
    test:ok(string.find(err_msg, 'comma') ~= nil, 'comma instead of T_COMMA')
    _, err_msg = pcall(serializer.decode, '{')
    test:ok(string.find(err_msg, 'end') ~= nil, 'end instead of T_END')

    --
    -- gh-4339: Make sure that context is printed.
    --
    _, err_msg = pcall(serializer.decode, '{{: "world"}')
    test:ok(string.find(err_msg, '{ >> {: "worl') ~= nil, 'context #1')
    _, err_msg = pcall(serializer.decode, '{"a": "world"}}')
    test:ok(string.find(err_msg, '"world"} >> }') ~= nil, 'context #2')
    _, err_msg = pcall(serializer.decode, '{1: "world"}')
    test:ok(string.find(err_msg, '{ >> 1: "worl') ~= nil, 'context #3')
    _, err_msg = pcall(serializer.decode, '{')
    test:ok(string.find(err_msg, '{ >> ') ~= nil, 'context #4')
    _, err_msg = pcall(serializer.decode, '}')
    test:ok(string.find(err_msg, ' >> }') ~= nil, 'context #5')
    serializer.cfg{decode_max_depth = 1}
    _, err_msg = pcall(serializer.decode, '{"a": {a = {}}}')
    test:ok(string.find(err_msg, '{"a":  >> {a = {}}') ~= nil, 'context #6')

    --
    -- Create a big JSON string to ensure the string builder works fine with
    -- internal reallocs.
    --
    local bigstr = string.rep('a', 16384)
    local t = {}
    for _ = 1, 10 do
        table.insert(t, bigstr)
    end
    local bigjson = serializer.encode(t)
    local t_dec = serializer.decode(bigjson)
    test:is_deeply(t_dec, t, 'encode/decode big strings')

    --
    -- Test encode_key_order option.
    --
    local map_1 = {a = 1, c = 100, b = 2}
    local map_2 = {a = 1, c = 100, b = 2, inner = {a = 1, c = 100, b = 2}}
    local map_3 = {[12] = 'number', a = 1, c = 100, b = 2}

    test:is(serializer.encode(map_1, {encode_key_order = {'a', 'b', 'c'}}),
            '{"a":1,"b":2,"c":100}',
            'custom order')
    test:is(serializer.encode(map_1, {encode_key_order = {'a', 'b'}}),
            '{"a":1,"b":2,"c":100}',
            'partial custom order')
    test:is(serializer.encode(map_1, {encode_key_order = {'a', 'b', 'd'}}),
            '{"a":1,"b":2,"c":100}',
            'unknown key in custom order')

    test:is(serializer.encode(map_2, {encode_key_order = {'a', 'b', 'c'}}),
            '{"a":1,"b":2,"c":100,"inner":{"a":1,"b":2,"c":100}}',
            'partial custom order with nested map')
    test:is(serializer.encode(map_2, {encode_key_order = {'a', 'b', 'inner'}}),
            '{"a":1,"b":2,"inner":{"a":1,"b":2,"c":100},"c":100}',
            'nested map in custom order')

    test:is(serializer.encode(map_3, {encode_key_order = {'a', 'b', 'c'}}),
            '{"a":1,"b":2,"c":100,"12":"number"}',
            'partial custom order with number key')
    test:is(serializer.encode(map_3, {encode_key_order = {12, 'a', 'b'}}),
            '{"12":"number","a":1,"b":2,"c":100}',
            'number key in custom order')
    test:is(serializer.encode(map_3, {encode_key_order = {{}, 'a', {},'b', 12}}),
            '{"a":1,"b":2,"12":"number","c":100}',
            'non-string key in custom order')

    local j = serializer.new()
    test:is(j.encode(map_1, {encode_key_order = {'a', 'b', 'c'}}),
            '{"a":1,"b":2,"c":100}',
            'custom order with new instance')
    test:is(j.encode(map_1, {encode_key_order = {'a', 'b'}}),
            '{"a":1,"b":2,"c":100}',
            'partial custom order with new instance')

    j.cfg({encode_key_order = {'a', 'b'}})
    test:is(j.encode(map_1),
            '{"a":1,"b":2,"c":100}',
            'custom order with .cfg')
    test:is(j.encode(map_1, {encode_key_order = {'b', 'c'}}),
            '{"b":2,"c":100,"a":1}',
            'custom order with .cfg and .encode')
    test:is(j.encode(map_1),
            '{"a":1,"b":2,"c":100}',
            'custom order with .cfg after .encode')
    test:is(j.encode(map_1, {}),
            '{"a":1,"b":2,"c":100}',
            'custom order with .cfg after .encode and empty options in .encode')
    test:is(j.encode({a = 1, d = 2, e = 3, f = 4},
            {encode_key_order = {'a', 'b', 'c', 'd', 'e', 'f'}}),
            '{"a":1,"d":2,"e":3,"f":4}',
            'custom order with many missed keys')

    local function new_mobj(v)
        return setmetatable({v = v}, {__serialize = function(o)
            return tostring(o.v)
        end})
    end
    local m1, m2, m3, m4 = new_mobj(1), new_mobj(2), new_mobj(3), new_mobj(4)
    test:is(j.encode({[m1] = 'a', [m2] = 'b', [m3] = 'c', [m4] = 'd'},
                     {encode_key_order = {m1, m2, m3, m4}}),
            '{"1":"a","2":"b","3":"c","4":"d"}', 'custom order for objects')

    test:is(j.encode({['1'] = 'a', [1] = 'b'}, {encode_key_order = {1, '1'}}),
            '{"1":"b","1":"a"}',
            'custom order for number and string which serialized identically')

    collectgarbage()
    test:is_deeply(j.cfg.encode_key_order,
                   {'a', 'b'},
                   'cfg option is persistent')

    -- Test possible Lua reference leak.
    local alive = 0
    local function make_marker()
        local ud = newproxy(true)
        getmetatable(ud).__gc = function()
            alive = alive - 1
        end
        alive = alive + 1
        return ud
    end

    -- luacheck: no unused
    local jleak = serializer.new()
    local m1 = make_marker()
    jleak.cfg({encode_key_order = {m1}})

    -- Overwrite `encode_key_order`. Now table with `m1` should
    -- be unref-ed.
    local m2 = make_marker()
    jleak.cfg({encode_key_order = {m2}})
    -- Registry holds `{m2}`.

    -- Remove markers.
    m1 = nil
    m2 = nil
    collectgarbage()
    collectgarbage()

    -- `{m2}` is still alive. Test that `{m1}` is collected.
    test:is(alive, 1, 'Only one instance (with m2) is alive')

    -- Test that we release all objects.
    jleak = nil
    collectgarbage()
    collectgarbage()

    test:is(alive, 0, 'all objects deleted')

    local map_4 = {[-1] = -1, [0] = 0, [1] = 1}
    local function sort_ascending(a, b)
        return a < b
    end
    local function sort_descending(a, b)
        return a > b
    end
    test:is(j.encode(map_4, {encode_key_order = sort_ascending}),
            '{"-1":-1,"0":0,"1":1}', 'custom order function ascending')
    test:is(j.encode(map_4, {encode_key_order = sort_descending}),
            '{"1":1,"0":0,"-1":-1}', 'custom order function descending')

    -- Memory leak tests when encode fails with encode_key_order set.
    _, err_msg = pcall(j.encode, {a = 1, [{}] = 2},
                       {encode_key_order = {'a'}})
    test:is(err_msg, 'table key must be a number or string',
            'mem-leak test for .encode error with persistent cfg')
    _, err_msg = pcall(serializer.encode, {a = 1, [{}] = 2},
                       {encode_key_order = {'a'}})
    test:is(err_msg, 'table key must be a number or string',
            'mem-leak test for .encode error with options')
    _, err_msg = pcall(j.decode, 'a',
                       {encode_key_order = {'a'}})
    test:is(err_msg,
            "Expected value but found invalid token on line 1 at character 1 here ' >> a'",
            'mem-leak test for .decode error with persistent cfg')
    _, err_msg = pcall(serializer.decode, 'a',
                       {encode_key_order = {'a'}})
    test:is(err_msg,
            "Expected value but found invalid token on line 1 at character 1 here ' >> a'",
            'mem-leak test for .decode error with options')
    _, err_msg = pcall(j.encode, {}, {encode_key_order = 1})
    test:is(err_msg, 'encode_key_order option must be a table or function')
    --
    -- gh-12726: Check that we used an original key while
    -- iterating, not its serialized form.
    --
    local meta_obj = setmetatable({}, {__serialize = function()
        return 'serialized'
    end})
    test:is(j.encode({[meta_obj] = 'value'}), '{"serialized":"value"}',
            'encoding with serialized object as the key')
end)

os.exit(test:check() and 0 or 1)
