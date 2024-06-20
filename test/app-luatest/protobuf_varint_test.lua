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
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_varint_exception_type_str = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Field "val" of "int32" type gets "string" type value.'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_varint_exception_type_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input cdata value "ctype<float>" ' ..
        'for "val" field is not integer'
    local data = {val = ffi.cast('float', 0.5)}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_varint_not_exception_number_ot_of_range = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local data = 2^54
    local result = protocol:encode('test', {val = data})
    t.assert_equals(string.hex(result), '088080808080808020')
end

g.test_int32_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protocol:encode('test', {val = 0})
    t.assert_equals(string.hex(result), '0800')
end

g.test_int32_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protocol:encode('test', {val = 1540})
    t.assert_equals(string.hex(result), '08840c')
end

g.test_int32_negative = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protocol:encode('test', {val = -2})
    t.assert_equals(string.hex(result), '08feffffffffffffffff01')
end

g.test_int32_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protocol:encode('test', {val = 2^31-1})
    t.assert_equals(string.hex(result), '08ffffffff07')
end

g.test_int32_min_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protocol:encode('test', {val = -2^31})
    t.assert_equals(string.hex(result), '0880808080f8ffffffff01')

end

g.test_int32_exception_pos = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input data for "val" field is "2147483648" ' ..
        'and do not fit in "int32"'
    local data = {val = 2^31}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_int32_exception_NaN = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input data for "val" field is NaN'
    local data = {val = 0/0}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_int32_exception_inf = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input data for "val" field is inf'
    local data = {val = 1/0}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_int32_exception_neg_inf = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input data for "val" field is inf'
    local data = {val = -1/0}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_int32_exception_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input data for "val" field is "-2147483649" ' ..
        'and do not fit in "int32"'
    local data = {val = -2^31-1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_int32_exception_cdata_pos = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input data for "val" field is "2147483648LL" ' ..
        'and do not fit in "int32"'
    local data = {val = 2LL^31}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_int32_exception_cdata_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Input data for "val" field is "-2147483649LL" ' ..
        'and do not fit in "int32"'
    local data = {val = -2LL^31 - 1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sint32_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local result = protocol:encode('test', {val = 0})
    t.assert_equals(string.hex(result), '0800')
end

g.test_sint32_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local result = protocol:encode('test', {val = 1540})
    t.assert_equals(string.hex(result), '088818')
end

g.test_sint32_negative = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local result = protocol:encode('test', {val = -770})
    t.assert_equals(string.hex(result), '08830c')
end

g.test_sint32_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local result = protocol:encode('test', {val = 2^31-1})
    t.assert_equals(string.hex(result), '08feffffff0f')
end

g.test_sint32_min_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local result = protocol:encode('test', {val = -2^31})
    t.assert_equals(string.hex(result), '08ffffffff0f')
end

g.test_sint32_exception_pos = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local msg = 'Input data for "val" field is "2147483648" ' ..
        'and do not fit in "sint32"'
    local data = {val = 2^31}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sint32_exception_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local msg = 'Input data for "val" field is "-2147483649" ' ..
        'and do not fit in "sint32"'
    local data = {val = -2^31-1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sint32_exception_cdata_pos = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local msg = 'Input data for "val" field is "2147483648LL" ' ..
        'and do not fit in "sint32"'
    local data = {val = 2LL^31}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sint32_exception_cdata_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint32', 1}})
    })
    local msg = 'Input data for "val" field is "-2147483649LL" ' ..
        'and do not fit in "sint32"'
    local data = {val = -2LL^31-1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_uint32_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint32', 1}})
    })
    local result = protocol:encode('test', {val = 1540})
    t.assert_equals(string.hex(result), '08840c')
end

g.test_uint32_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint32', 1}})
    })
    local result = protocol:encode('test', {val = 2^32-1})
    t.assert_equals(string.hex(result), '08ffffffff0f')
end

g.test_uint32_exception_poz = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint32', 1}})
    })
    local msg = 'Input data for "val" field is "4294967296" ' ..
        'and do not fit in "uint32"'
    local data = {val = 2^32}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_uint32_exception_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint32', 1}})
    })
    local msg = 'Input data for "val" field is "-1" ' ..
        'and do not fit in "uint32"'
    local data = {val = -1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_uint32_exception_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint32', 1}})
    })
    local msg = 'Input data for "val" field is "4294967296LL" ' ..
        'and do not fit in "uint32"'
    local data = {val = 2LL^32}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_int64_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local result = protocol:encode('test', {val = 1540})
    t.assert_equals(string.hex(result), '08840c')
end

g.test_int64_negative = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local result = protocol:encode('test', {val = -770})
    t.assert_equals(string.hex(result), '08fef9ffffffffffffff01')
end

g.test_int64_cdata_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local data = {val = 2LL^63-1}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '08ffffffffffffffff7f')
end

g.test_int64_cdata_min_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local data = {val = -2LL^63}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '0880808080808080808001')
end

g.test_int64_number_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local data = {val = 2^63-513}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '0880f8ffffffffffff7f')
end

g.test_int64_number_min_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local data = {val = -2^63}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '0880808080808080808001')
end

g.test_int64_exception_poz = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local msg = 'Input data for "val" field is "9.2233720368548e+18" ' ..
        'and do not fit in "int64"'
    local data = {val = 2^63-512}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_int64_exception_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local msg = 'Input data for "val" field is "-9.2233720368548e+18" ' ..
        'and do not fit in "int64"'
    local data = {val = -2^63-1025}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_int64_exception_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local msg = 'Input data for "val" field is "9223372036854775808ULL" ' ..
        'and do not fit in "int64"'
    local data = {val = 2ULL^63}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_int64_exception_cdata_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int64', 1}})
    })
    local msg = 'Input cdata value "ctype<float>" for "val" ' ..
        'field is not integer'
    local data = {val = ffi.cast('float', 0.5)}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sint64_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local result = protocol:encode('test', {val = 0})
    t.assert_equals(string.hex(result), '0800')
end

g.test_sint64_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local result = protocol:encode('test', {val = 1540})
    t.assert_equals(string.hex(result), '088818')
end

g.test_sint64_negative = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local result = protocol:encode('test', {val = -770})
    t.assert_equals(string.hex(result), '08830c')
end

g.test_sint64_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local data = {val = 2LL^63-1}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '08feffffffffffffffff01')
end

g.test_sint64_min_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local data = {val = -2LL^63}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '08ffffffffffffffffff01')
end

g.test_sint64_exception_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local msg = 'Input data for "val" field is "9223372036854775808ULL" ' ..
        'and do not fit in "sint64"'
    local data = {val = 2ULL^63}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sint64_exception_cdata_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sint64', 1}})
    })
    local msg = 'Input cdata value "ctype<float>" for "val" ' ..
        'field is not integer'
    local data = {val = ffi.cast('float', 0.5)}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_uint64_positive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint64', 1}})
    })
    local result = protocol:encode('test', {val = 2^32})
    t.assert_equals(string.hex(result), '088080808010')
end

g.test_uint64_max_num = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint64', 1}})
    })
    local data = {val = 2ULL^64-1}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '08ffffffffffffffffff01')
end

g.test_uint64_exception_neg = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint64', 1}})
    })
    local msg = 'Input data for "val" field is "-1" ' ..
        'and do not fit in "uint64"'
    local data = {val = -1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_uint64_exception_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'uint64', 1}})
    })
    local msg = 'Input data for "val" field is "-1LL" ' ..
        'and do not fit in "uint64"'
    local data = {val = -1LL}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_bool_true = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'bool', 1}})
    })
    local result = protocol:encode('test', {val = true})
    t.assert_equals(string.hex(result), '0801')
end

g.test_bool_false = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'bool', 1}})
    })
    local result = protocol:encode('test', {val = false})
    t.assert_equals(string.hex(result), '0800')
end

g.test_bool_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'bool', 1}})
    })
    local msg = 'Field "val" of "bool" type gets "string" type value.'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_bool_exception_value = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'bool', 1}})
    })
    local msg = 'Field "val" of "bool" type gets "number" type value.'
    local data = {val = 2}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end
