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

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_range = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {format = {
            {'i8', 'int8'},
            {'i16', 'int16'},
            {'i32', 'int32'},
            {'i64', 'int64'},
            {'u8', 'uint8'},
            {'u16', 'uint16'},
            {'u32', 'uint32'},
        }})
        local errmsg =
                'The value of key part exceeds the supported range for type'
        local test_signed_size = function(bits)
            local exp = bits - 1
            local index = s:create_index('test', {parts = {'i' .. bits}})
            t.assert_error_covers({
                code = box.error.KEY_PART_VALUE_OUT_OF_RANGE,
                key = {-2^exp - 1},
                message = errmsg,
                partno = 0,
                field_type = 'int' .. bits,
                value = -2^exp - 1,
                min = -2^exp,
                max = 2^exp - 1,
            }, index.get, index, {-2^exp - 1})
            t.assert_equals(index:get{-2^exp}, nil)
            t.assert_error_covers({
                code = box.error.KEY_PART_VALUE_OUT_OF_RANGE,
                key = {2^exp},
                message = errmsg,
                partno = 0,
                field_type = 'int' .. bits,
                value = 2^exp,
                min = -2^exp,
                max = 2^exp - 1,
            }, index.get, index, {2^exp})
            t.assert_equals(index:get{2^exp - 1}, nil)
            index:drop()
        end
        local test_unsigned_size = function(bits)
            local exp = bits
            local index = s:create_index('test', {parts = {'u' .. bits}})
            t.assert_error_covers({
                code = box.error.KEY_PART_VALUE_OUT_OF_RANGE,
                key = {2^exp},
                message = errmsg,
                partno = 0,
                field_type = 'uint' .. bits,
                value = 2^exp,
                min = 0,
                max = 2^exp - 1,
            }, index.get, index, {2^exp})
            t.assert_equals(index:get{2^exp - 1}, nil)
            index:drop()
        end
        test_signed_size(8)
        test_signed_size(16)
        test_signed_size(32)
        test_unsigned_size(8)
        test_unsigned_size(16)
        test_unsigned_size(32)
        local index = s:create_index('test', {parts = {'i64'}})
        t.assert_error_covers({
            code = box.error.KEY_PART_VALUE_OUT_OF_RANGE,
            key = {9223372036854775808ULL},
            message = errmsg,
            partno = 0,
            field_type = 'int64',
            value = 9223372036854775808ULL,
            min = -9223372036854775808LL,
            max = 9223372036854775807LL,
        }, index.get, index, {9223372036854775808ULL})
        t.assert_equals(index:get{9223372036854775807ULL}, nil)
    end)
end

g.test_error_partno = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {format = {
            {'f1', 'uint8'},
            {'f2', 'int8'},
        }})
        local index = s:create_index('test', {parts = {'f1', 'f2'}})
        -- Test `partno` is correct.
        t.assert_error_covers({
            code = box.error.KEY_PART_VALUE_OUT_OF_RANGE,
            key = {0, 2^7},
            partno = 1,
            field_type = 'int8',
            value = 2^7,
            min = -2^7,
            max = 2^7 - 1,
        }, index.get, index, {0, 2^7})
    end)
end

g.test_null_value = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {format = {
            {'f1', 'unsigned'},
            {'f2', 'uint8', is_nullable = true},
        }})
        s:create_index('pk')
        local index = s:create_index('sk', {parts = {'f2'}})
        -- Use count as it allows nullable parts.
        t.assert_equals(index:count({box.NULL}), 0)
    end)
end
