#!/usr/bin/env tarantool

package.path = "lua/?.lua;"..package.path

local ffi = require('ffi')
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
    test:plan(28)

    test:test("unsigned", common.test_unsigned, serializer)
    test:test("signed", common.test_signed, serializer)
    test:test("double", common.test_double, serializer)
    test:test("boolean", common.test_boolean, serializer)
    test:test("string", common.test_string, serializer)
    test:test("nil", common.test_nil, serializer)
    test:test("table", common.test_table, serializer, is_array, is_map)
    test:test("ucdata", common.test_ucdata, serializer)
    test:test("misc", test_misc, serializer)

    --
    -- gh-2888: Check the possibility of using options in encode()/decode().
    --
    local orig_encode_max_depth = serializer.cfg.encode_max_depth
    local sub = {a = 1, { b = {c = 1, d = {e = 1}}}}
    serializer.cfg({encode_max_depth = 1})
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
end)
