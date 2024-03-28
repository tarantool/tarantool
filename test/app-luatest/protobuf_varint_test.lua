local ffi = require('ffi')
local t = require('luatest')
local protobuf = require('protobuf')
local g = t.group()

g.test_varint_exception_type_float = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input number value 1.500000 for "val" is not integer'
    local data = {val = 1.5}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_varint_exception_type_str = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Field "val" of "int32" type gets "string" type value. ' ..
        'Unsupported or colliding types'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_varint_exception_type_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input cdata value "ctype<float>" ' ..
        'for "val" field is not integer'
    local data = {val = ffi.cast('float', 0.5)}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_varint_not_exception_number_ot_of_range = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local data = 19007199254740994
    local result = protobuf.encode(protocol, 'test', {val = data})
    t.assert_equals(string.hex(result), '08808084fea6dee121')
end

g.test_int32_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 0})
    t.assert_equals(string.hex(result), '0800')
end

g.test_int32_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 1540})
    t.assert_equals(string.hex(result), '08840c')
end

g.test_int32_negative = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = -2})
    t.assert_equals(string.hex(result), '08feffffffffffffffff01')
end

g.test_int32_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 2147483647})
    t.assert_equals(string.hex(result), '08ffffffff07')
end

g.test_int32_min_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = -2147483647})
    t.assert_equals(string.hex(result), '0881808080f8ffffffff01')

end

g.test_int32_exception_poz = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input data for "val" field is 2147483648 ' ..
        'and do not fit in sint_32'
    local data = {val = 2147483648}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_int32_exception_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input data for "val" field is -2147483650 ' ..
        'and do not fit in sint_32'
    local data = {val = -2147483650}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_int32_exception_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input data for "val" field is 2147483648 ' ..
        'and do not fit in sint_32'
    local data = {val = 2147483648LL}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_sint32_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 1540})
    t.assert_equals(string.hex(result), '08840c')
end

g.test_sint32_negative = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = -770})
    t.assert_equals(string.hex(result), '08830c')
end

g.test_sint32_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 2147483647})
    t.assert_equals(string.hex(result), '08ffffffff07')
end

g.test_sint32_min_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = -2147483647})
    t.assert_equals(string.hex(result), '08fdffffff0f')
end

g.test_sint32_exception_poz = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local msg = 'Input data for "val" field is 2147483650 ' ..
        'and do not fit in sint_32'
    local data = {val = 2147483650}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_sint32_exception_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local msg = 'Input data for "val" field is -2147483650 ' ..
        'and do not fit in sint_32'
    local data = {val = -2147483650}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_sint32_exception_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local msg = 'Input data for "val" field is -2147483650 ' ..
        'and do not fit in sint_32'
    local data = {val = -2147483650LL}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_uint32_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 1540})
    t.assert_equals(string.hex(result), '08840c')
end

g.test_uint32_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 4294967295})
    t.assert_equals(string.hex(result), '08ffffffff0f')
end

g.test_uint32_exception_poz = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint32', 1}})
    })
    local msg = 'Input data for "val" field is 4294967296 ' ..
        'and do not fit in uint_32'
    local data = {val = 4294967296}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_uint32_exception_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint32', 1}})
    })
    local msg = 'Input data for "val" field is -1 ' ..
        'and do not fit in unsigned type'
    local data = {val = -1}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_uint32_exception_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint32', 1}})
    })
    local msg = 'Input data for "val" field is 4294967296 ' ..
        'and do not fit in uint_32'
    local data = {val = 4294967296LL}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_int64_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 1540})
    t.assert_equals(string.hex(result), '08840c')
end

g.test_int64_negative = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = -770})
    t.assert_equals(string.hex(result), '08fef9ffffffffffffff01')
end

g.test_int64_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local data = {val = 9223372036854775807LL}
    local result = protobuf.encode(protocol, 'test', data)
    t.assert_equals(string.hex(result), '08ffffffffffffffff7f')
end

g.test_int64_min_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local data = {val = -9223372036854775808LL}
    local result = protobuf.encode(protocol, 'test', data)
    t.assert_equals(string.hex(result), '0880808080808080808001')
end

g.test_sint64_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 1540})
    t.assert_equals(string.hex(result), '08840c')
end

g.test_sint64_negative = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = -770})
    t.assert_equals(string.hex(result), '08830c')
end

g.test_sint64_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local data = {val = 9223372036854775807LL}
    local result = protobuf.encode(protocol, 'test', data)
    t.assert_equals(string.hex(result), '08ffffffffffffffff7f')
end

g.test_sint64_min_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local data = {val = -9223372036854775808LL}
    local result = protobuf.encode(protocol, 'test', data)
    t.assert_equals(string.hex(result), '08ffffffffffffffffff01')
end

g.test_uint64_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 2147483500ULL})
    t.assert_equals(string.hex(result), '08ecfeffff07')
end

g.test_uint64_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint64', 1}})
    })
    local data = {val = 18446744073709551615ULL}
    local result = protobuf.encode(protocol, 'test', data)
    t.assert_equals(string.hex(result), '08ffffffffffffffffff01')
end

g.test_uint64_exception_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint64', 1}})
    })
    local msg = 'Input data for "val" field is -1 ' ..
        'and do not fit in unsigned type'
    local data = {val = -1}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_bool_true = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'bool', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = true})
    t.assert_equals(string.hex(result), '0801')
end

g.test_bool_false = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'bool', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = false})
    t.assert_equals(string.hex(result), '0800')
end
