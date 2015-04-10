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
    test:plan(9)
    test:test("unsigned", common.test_unsigned, serializer)
    test:test("signed", common.test_signed, serializer)
    test:test("double", common.test_double, serializer)
    test:test("boolean", common.test_boolean, serializer)
    test:test("string", common.test_string, serializer)
    test:test("nil", common.test_nil, serializer)
    test:test("table", common.test_table, serializer, is_array, is_map)
    test:test("ucdata", common.test_ucdata, serializer)
    test:test("misc", test_misc, serializer)
end)
