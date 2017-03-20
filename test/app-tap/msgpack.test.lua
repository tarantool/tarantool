#!/usr/bin/env tarantool

package.path = "lua/?.lua;"..package.path

local tap = require('tap')
local common = require('serializer_test')

local function is_map(s)
    local b = string.byte(string.sub(s, 1, 1))
    return b >= 0x80 and b <= 0x8f or b == 0xde or b == 0xdf
end

local function is_array(s)
    local b = string.byte(string.sub(s, 1, 1))
    return b >= 0x90 and b <= 0x9f or b == 0xdc or b == 0xdd
end

local function test_offsets(test, s)
    test:plan(6)
    local arr1 = {1, 2, 3}
    local arr2 = {4, 5, 6}
    local dump = s.encode(arr1)..s.encode(arr2)
    test:is(dump:len(), 8, "length of part1 + part2")

    local a
    local offset = 1
    a, offset = s.decode(dump, offset)
    test:is_deeply(a, arr1, "decoded part1")
    test:is(offset, 5, "offset of part2")

    a, offset = s.decode(dump, offset)
    test:is_deeply(a, arr2, "decoded part2")
    test:is(offset, 9, "offset of end")

    test:ok(not pcall(s.decode, dump, offset), "invalid offset")
end

local function test_misc(test, s)
    test:plan(3)
    local ffi = require('ffi')
    local buf = ffi.cast("const char *", "\x91\x01")
    local bufcopy = ffi.cast('const char *', buf)
    local bufend, result = s.ibuf_decode(buf)
    test:is(buf, bufcopy, "ibuf_decode argument is constant")
    test:is(buf + 2, bufend, 'ibuf_decode position')
    test:is_deeply(result, {1}, "ibuf_decode result")
end

tap.test("msgpack", function(test)
    local serializer = require('msgpack')
    test:plan(10)
    test:test("unsigned", common.test_unsigned, serializer)
    test:test("signed", common.test_signed, serializer)
    test:test("double", common.test_double, serializer)
    test:test("boolean", common.test_boolean, serializer)
    test:test("string", common.test_string, serializer)
    test:test("nil", common.test_nil, serializer)
    test:test("table", common.test_table, serializer, is_array, is_map)
    test:test("ucdata", common.test_ucdata, serializer)
    test:test("offsets", test_offsets, serializer)
    test:test("misc", test_misc, serializer)
end)
