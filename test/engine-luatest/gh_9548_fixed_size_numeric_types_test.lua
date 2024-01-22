local t = require('luatest')
local server = require('luatest.server')

local function before_all(cg)
    cg.server = server:new()
    cg.server:start()
end

local function after_all(cg)
    cg.server:drop()
end

local function before_each(cg)
    cg.server:exec(function(engine)
        box.schema.space.create('test', {engine = engine})
        box.space.test:create_index('pk')
    end, {cg.params.engine})
end

local function after_each(cg)
    cg.server:exec(function()
        box.space.test:drop()
        box.schema.func.drop('foo', {if_exists = true})
    end)
end

local g1 = t.group('gh-9548-1', {{engine = 'memtx'}, {engine = 'vinyl'}})
g1.before_all(before_all)
g1.after_all(after_all)
g1.before_each(before_each)
g1.after_each(after_each)

-- Test the limits of fixed-size unsigned integer types.
local function test_fixed_size_unsigned(cg, params)
    cg.server:exec(function(params)
        local ffi = require('ffi')
        local s = box.space.test
        s:format({{name = 'pk',  type = 'unsigned'},
                  {name = 'u8',  type = 'uint8'   },
                  {name = 'u16', type = 'uint16'  },
                  {name = 'u32', type = 'uint32'  },
                  {name = 'u64', type = 'uint64'  },
                  {name = 'map', type = 'map'     }})

        if params.has_indexed_json_path then
            s:create_index('sk', {parts = {{field = 'map', type = 'string',
                                            path = 'xyz'}}})
        end

        -- Insert maximum possible values into each field.
        s:replace({0, 2^8-1, 2^16-1, 2^32-1, ffi.cast('uint64_t', 2^64-1),
                   {xyz = 'abc'}})

        -- Insert out-of-range values.
        t.assert_error_msg_equals(
            "The value of field 2 (u8) exceeds the supported range for " ..
            "type 'uint8': expected [0..255], got 256",
            s.update, s, 0, {{'=', 'u8', 2^8}})
        t.assert_error_msg_equals(
            "The value of field 3 (u16) exceeds the supported range for " ..
            "type 'uint16': expected [0..65535], got 65536",
            s.update, s, 0, {{'=', 'u16', 2^16}})
        t.assert_error_msg_equals(
            "The value of field 4 (u32) exceeds the supported range for " ..
            "type 'uint32': expected [0..4294967295], got 4294967296",
            s.update, s, 0, {{'=', 'u32', 2^32}})
        -- It's not possible to exceed the limit of uint64.
    end, {params})
end

-- Test the limits of fixed-size unsigned integer types.
-- Tuple format doesn't contain fields accessed by JSON paths.
g1.test_fixed_size_unsigned_plain = function(cg)
    test_fixed_size_unsigned(cg, {has_indexed_json_path = false})
end

-- Test the limits of fixed-size unsigned integer types.
-- Tuple format contains a field accessed by JSON path.
g1.test_fixed_size_unsigned_json = function(cg)
    test_fixed_size_unsigned(cg, {has_indexed_json_path = true})
end

-- Test the limits of fixed-size signed integer types.
local function test_fixed_size_signed(cg, params)
    cg.server:exec(function(params)
        local ffi = require('ffi')
        local s = box.space.test
        s:format({{name = 'pk',  type = 'unsigned'},
                  {name = 'i8',  type = 'int8'    },
                  {name = 'i16', type = 'int16'   },
                  {name = 'i32', type = 'int32'   },
                  {name = 'i64', type = 'int64'   },
                  {name = 'map', type = 'map'     }})

        if params.has_indexed_json_path then
            s:create_index('sk', {parts = {{field = 'map', type = 'string',
                                            path = 'xyz'}}})
        end

        -- Insert minimum possible values into each field.
        s:replace({0, -2^8/2, -2^16/2, -2^32/2,
                   ffi.cast('int64_t', -2^64/2), {xyz = 'abc'}})

        -- Insert maximum possible values into each field.
        s:replace({0, 2^8/2-1, 2^16/2-1, 2^32/2-1,
                   ffi.cast('int64_t', 2^64/2-1), {xyz = 'abc'}})

        -- Insert out-of-range negative values.
        t.assert_error_msg_equals(
            "The value of field 2 (i8) exceeds the supported range for " ..
            "type 'int8': expected [-128..127], got -129",
            s.update, s, 0, {{'=', 'i8', -2^8/2-1}})
        t.assert_error_msg_equals(
            "The value of field 3 (i16) exceeds the supported range for " ..
            "type 'int16': expected [-32768..32767], got -32769",
            s.update, s, 0, {{'=', 'i16', -2^16/2-1}})
        t.assert_error_msg_equals(
            "The value of field 4 (i32) exceeds the supported range for " ..
            "type 'int32': expected [-2147483648..2147483647], got -2147483649",
            s.update, s, 0, {{'=', 'i32', -2^32/2-1}})
        -- It's not possible to exceed the negative limit of int64.

        -- Insert out-of-range positive values.
        t.assert_error_msg_equals(
            "The value of field 2 (i8) exceeds the supported range for " ..
            "type 'int8': expected [-128..127], got 128",
            s.update, s, 0, {{'=', 'i8', 2^8/2}})
        t.assert_error_msg_equals(
            "The value of field 3 (i16) exceeds the supported range for " ..
            "type 'int16': expected [-32768..32767], got 32768",
            s.update, s, 0, {{'=', 'i16', 2^16/2}})
        t.assert_error_msg_equals(
            "The value of field 4 (i32) exceeds the supported range for " ..
            "type 'int32': expected [-2147483648..2147483647], got 2147483648",
            s.update, s, 0, {{'=', 'i32', 2^32/2}})
        t.assert_error_msg_equals(
            "The value of field 5 (i64) exceeds the supported range for " ..
            "type 'int64': expected [-9223372036854775808.." ..
            "9223372036854775807], got 9223372036854775808",
            s.update, s, 0, {{'=', 'i64', 2^64/2}})
    end, {params})
end

-- Test the limits of fixed-size signed integer types.
-- Tuple format doesn't contain fields accessed by JSON paths.
g1.test_fixed_size_signed_plain = function(cg)
    test_fixed_size_signed(cg, {has_indexed_json_path = false})
end

-- Test the limits of fixed-size signed integer types.
-- Tuple format contains a field accessed by JSON path.
g1.test_fixed_size_signed_json = function(cg)
    test_fixed_size_signed(cg, {has_indexed_json_path = true})
end

-- Test fixed-size floating point types. There is no value range check.
-- float32 can store only MP_FLOAT, and float64 can store only MP_DOUBLE.
g1.test_fixed_size_float = function(cg)
    cg.server:exec(function()
        local ffi = require('ffi')
        local s = box.space.test
        s:format({{name = 'pk',  type = 'unsigned'},
                  {name = 'f32', type = 'float32' },
                  {name = 'f64', type = 'float64' }})
        local float32 = ffi.cast('float', 12.34)
        local float64 = ffi.cast('double', 56.78)
        s:insert({0, float32, float64})
        t.assert_error_msg_equals(
            "Tuple field 2 (f32) type does not match one required by " ..
            "operation: expected float32, got double",
            s.insert, s, {1, float64, float64})
        t.assert_error_msg_equals(
            "Tuple field 3 (f64) type does not match one required by " ..
            "operation: expected float64, got float",
            s.insert, s, {1, float32, float32})
    end)
end

-- Check that value range check works with nullable fields.
g1.test_nullable = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        s:format({{name = 'pk',  type = 'unsigned'},
                  {name = 'i8',  type = 'int8', is_nullable = true},
                  {name = 'u32', type = 'uint32', is_nullable = true}})
        s:insert({0, -100, box.NULL})
        s:insert({1, nil, 100000000})
    end)
end

-- Check that value range check works with default field values.
g1.test_default_values = function(cg)
    cg.server:exec(function()
        box.schema.func.create('foo', {
            language = 'Lua',
            is_deterministic = false,
            body = 'function() return -2200000000 end'
        })
        local s = box.space.test
        s:format({{name = 'pk',  type = 'unsigned'},
                  {name = 'u16', type = 'uint16', default = 66000},
                  {name = 'i32', type = 'int32', default_func = 'foo'}})
        t.assert_error_msg_equals(
            "The value of field 2 (u16) exceeds the supported range for " ..
            "type 'uint16': expected [0..65535], got 66000",
            s.insert, s, {0, nil, 0})
        t.assert_error_msg_equals(
            "The value of field 3 (i32) exceeds the supported range for " ..
            "type 'int32': expected [-2147483648..2147483647], got -2200000000",
            s.insert, s, {0, 0})
    end)
end

-- Test space format change with various combinations of old/new field type.
g1.test_format_change = function(cg)
    cg.server:exec(function()
        local ffi = require('ffi')
        local s = box.space.test
        local testcases = {
            {
                type = 'unsigned',
                value = 100,
                allow = {'int8', 'uint8', 'int16', 'uint16', 'int32', 'uint32',
                         'int64', 'uint64'},
                forbid = {'float64'},
                error = "type does not match one required by operation: " ..
                        "expected float64, got unsigned"
            }, {
                type = 'unsigned',
                value = 1000,
                allow = {'int16', 'uint16', 'int32', 'uint32', 'int64',
                         'uint64'},
                forbid = {'int8', 'uint8'},
                error = "exceeds the supported range for type"
            }, {
                type = 'unsigned',
                value = 100000,
                allow = {'int32', 'uint32', 'int64', 'uint64'},
                forbid = {'int8', 'uint8', 'int16', 'uint16'},
                error = "exceeds the supported range for type"
            }, {
                type = 'unsigned',
                value = 10000000000,
                allow = {'int64', 'uint64'},
                forbid = {'int8', 'uint8', 'int16', 'uint16', 'int32',
                          'uint32'},
                error = "exceeds the supported range for type"
            }, {
                type = 'uint8',
                value = 200,
                allow = {'any', 'int16', 'uint16', 'int32', 'uint32', 'int64',
                         'uint64', 'unsigned', 'number', 'integer', 'scalar'},
                forbid = {'int8'},
                error = "exceeds the supported range for type 'int8'"
            }, {
                type = 'int64',
                value = -100,
                allow = {'int8', 'int16', 'int32', 'any', 'number', 'integer'},
                forbid = {'uint8', 'uint16', 'uint32', 'uint64', 'unsigned',
                          'string', 'boolean', 'varbinary', 'decimal', 'uuid',
                          'datetime', 'interval', 'array', 'map'},
                error = "type does not match one required by operation"
            }, {
                type = 'number',
                value = -1000,
                allow = {'int16', 'int32', 'int64'},
                forbid = {'int8'},
                error = "exceeds the supported range for type 'int8'"
            }, {
                type = 'scalar',
                value = -100000,
                allow = {'int32', 'int64'},
                forbid = {'int8', 'int16'},
                error = "exceeds the supported range for type"
            }, {
                type = 'integer',
                value = -10000000000,
                allow = {'int64'},
                forbid = {'int8', 'int16', 'int32'},
                error = "exceeds the supported range for type"
            }, {
                type = 'double',
                value = ffi.cast('float', 123.456),
                allow = {'float32'},
                forbid = {'float64'},
                error = "type does not match one required by operation: " ..
                        "expected float64, got float"
            }, {
                type = 'double',
                value = ffi.cast('double', 123.456),
                allow = {'float64'},
                forbid = {'float32'},
                error = "type does not match one required by operation: " ..
                        "expected float32, got double"
            }, {
                type = 'float32',
                value = ffi.cast('float', 123.456),
                allow = {'double', 'any'},
                forbid = {'float64'},
                error = "type does not match one required by operation: " ..
                        "expected float64, got float"
            }
        }

        for _, test in pairs(testcases) do
            -- Set field type to `test.type' and insert `test.value' into it.
            local format = {{name = 'pk', type = 'unsigned'},
                            {name = 'f2', type = test.type}}
            s:format(format)
            s:insert({0, test.value})
            -- Check that it is possible to change field type to `test.allow'.
            for _, type in pairs(test.allow) do
                format = {{name = 'pk', type = 'unsigned'},
                          {name = 'f2', type = type}}
                s:format(format)
            end
            -- Check that it's impossible to change field type to `test.forbid'.
            for _, type in pairs(test.forbid) do
                format = {{name = 'pk', type = 'unsigned'},
                          {name = 'f2', type = type}}
                t.assert_error_msg_contains(test.error, s.format, s, format)
            end
            s:truncate()
        end
    end)
end

local g2 = t.group('gh-9548-2', {
    {engine = 'memtx', index = 'tree', is_nullable = false},
    {engine = 'memtx', index = 'tree', is_nullable = true},
    {engine = 'memtx', index = 'hash', is_nullable = false},
    {engine = 'vinyl', index = 'tree', is_nullable = false},
    {engine = 'vinyl', index = 'tree', is_nullable = true}
})
g2.before_all(before_all)
g2.after_all(after_all)
g2.before_each(before_each)
g2.after_each(after_each)

-- Check that fixed-size numeric types can be indexed.
g2.test_indexed = function(cg)
    cg.server:exec(function(index_type, is_nullable)
        local ffi = require('ffi')
        local s = box.space.test
        s:format({{name = 'pk',  type = 'unsigned', is_nullable = is_nullable},
                  {name = 'i8',  type = 'int8',     is_nullable = is_nullable},
                  {name = 'u8',  type = 'uint8',    is_nullable = is_nullable},
                  {name = 'i16', type = 'int16',    is_nullable = is_nullable},
                  {name = 'u16', type = 'uint16',   is_nullable = is_nullable},
                  {name = 'i32', type = 'int32',    is_nullable = is_nullable},
                  {name = 'u32', type = 'uint32',   is_nullable = is_nullable},
                  {name = 'i64', type = 'int64',    is_nullable = is_nullable},
                  {name = 'u64', type = 'uint64',   is_nullable = is_nullable},
                  {name = 'f32', type = 'float32',  is_nullable = is_nullable},
                  {name = 'f64', type = 'float64',  is_nullable = is_nullable}})
        s:create_index('i8',  {type = index_type, parts = {{field = 'i8'}}})
        s:create_index('u8',  {type = index_type, parts = {{field = 'u8'}}})
        s:create_index('i16', {type = index_type, parts = {{field = 'i16'}}})
        s:create_index('u16', {type = index_type, parts = {{field = 'u16'}}})
        s:create_index('i32', {type = index_type, parts = {{field = 'i32'}}})
        s:create_index('u32', {type = index_type, parts = {{field = 'u32'}}})
        s:create_index('i64', {type = index_type, parts = {{field = 'i64'}}})
        s:create_index('u64', {type = index_type, parts = {{field = 'u64'}}})
        s:create_index('f32', {type = index_type, parts = {{field = 'f32'}}})
        s:create_index('f64', {type = index_type, parts = {{field = 'f64'}}})

        local tuple = {0, -40, 40, -2^14, 2^14, -2^30, 2^30, -2^35, 2^35,
                       ffi.cast('float', -0.5), ffi.cast('double', -1e100)}
        s:insert(tuple)
        t.assert_equals(s.index.i8:get(-40), tuple)
        t.assert_equals(s.index.u8:get(40), tuple)
        t.assert_equals(s.index.i16:get(-2^14), tuple)
        t.assert_equals(s.index.u16:get(2^14), tuple)
        t.assert_equals(s.index.i32:get(-2^30), tuple)
        t.assert_equals(s.index.u32:get(2^30), tuple)
        t.assert_equals(s.index.i64:get(-2^35), tuple)
        t.assert_equals(s.index.u64:get(2^35), tuple)
        t.assert_equals(s.index.f32:get(ffi.cast('float', -0.5)), tuple)
        t.assert_equals(s.index.f64:get(ffi.cast('double', -1e100)), tuple)
    end, {cg.params.index, cg.params.is_nullable})
end
