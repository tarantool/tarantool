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

tap.test("json", function(test)
    local serializer = require('json')
    test:plan(58)

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
end)
