local t = require('luatest')
local ffi = require('ffi')
local protobuf = require('protobuf')
local g = t.group()

g.test_float = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local result = protocol:encode('test', {val = 0.5})
    t.assert_equals(string.hex(result), '0d0000003f')
end

g.test_float_min = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local result = protocol:encode('test', {val = -0x1.fffffep+127})
    t.assert_equals(string.hex(result), '0dffff7fff')
end

g.test_float_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local result = protocol:encode('test', {val = 0x1.fffffep+127})
    t.assert_equals(string.hex(result), '0dffff7f7f')
end

g.test_float_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local msg = 'Field "val" of "float" type gets "string" type value.'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_float_exception_NaN = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local msg = 'Input data for "val" field is NaN'
    local data = {val = 0/0}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_float_exception_inf = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local msg = 'Input data for "val" field is inf'
    local data = {val = 1/0}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_float_exception_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local msg = 'Field "val" of "float" type gets "cdata" type value.'
    local data = {val = 15LL}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_float_exception_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local msg = 'Input data for "val" field is "3.402823467e+38" and ' ..
        'do not fit in "float"'
    local data = {val = 0x1.fffffe018d3f8p+127}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_fixed32_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local result = protocol:encode('test', {val = 0})
    t.assert_equals(string.hex(result), '0d00000000')
end

g.test_fixed32_number = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local result = protocol:encode('test', {val = 10})
    t.assert_equals(string.hex(result), '0d0a000000')
end

g.test_fixed32_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local result = protocol:encode('test', {val = 10ULL})
    t.assert_equals(string.hex(result), '0d0a000000')
end

g.test_fixed32_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local result = protocol:encode('test', {val = 2^32-1})
    t.assert_equals(string.hex(result), '0dffffffff')
end

g.test_fixed32_max_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local result = protocol:encode('test', {val = 2LL^32-1})
    t.assert_equals(string.hex(result), '0dffffffff')
end

g.test_fixed32_exception_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local msg = 'Input data for "val" field is "4294967296" and ' ..
        'do not fit in "fixed32"'
    local data = {val = 2^32}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_fixed32_exception_not_integral = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local msg = 'Input number value 10.500000 for "val" is not integer'
    local data = {val = 10.5}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_fixed32_exception_size_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local msg = 'Input data for "val" field is "4294967296LL" and ' ..
        'do not fit in "fixed32"'
    local data = {val = 2LL^32}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_fixed32_exception_type_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local msg = 'Input cdata value "ctype<float>" ' ..
        'for "val" field is not integer'
    local data = {val = ffi.cast('float', 0.5)}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_fixed32_exception_sign = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local msg = 'Input data for "val" field is "-1" and ' ..
        'do not fit in "fixed32"'
    local data = {val = -1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_fixed32_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local msg = 'Field "val" of "fixed32" type gets "string" type value.'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sfixed32_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protocol:encode('test', {val = 0})
    t.assert_equals(string.hex(result), '0d00000000')
end

g.test_sfixed32 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protocol:encode('test', {val = 10})
    t.assert_equals(string.hex(result), '0d0a000000')
end

g.test_sfixed32_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protocol:encode('test', {val = 10ULL})
    t.assert_equals(string.hex(result), '0d0a000000')
end

g.test_sfixed32_min = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protocol:encode('test', {val = -2^31})
    t.assert_equals(string.hex(result), '0d00000080')
end

g.test_sfixed32_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protocol:encode('test', {val = 2^31-1})
    t.assert_equals(string.hex(result), '0dffffff7f')
end

g.test_sfixed32_cdata_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protocol:encode('test', {val = 2LL^31-1})
    t.assert_equals(string.hex(result), '0dffffff7f')
end

g.test_sfixed32_cdata_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protocol:encode('test', {val = -2LL^31})
    t.assert_equals(string.hex(result), '0d00000080')
end

g.test_sfixed32_exception_not_integral = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local msg = 'Input number value 10.500000 for "val" is not integer'
    local data = {val = 10.5}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sfixed32_exception_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local msg = 'Input data for "val" field is "2147483648" ' ..
        'and do not fit in "sfixed32"'
    local data = {val = 2^31}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sfixed32_exception_neg_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local msg = 'Input data for "val" field is "-2147483649" ' ..
        'and do not fit in "sfixed32"'
    local data = {val = -2^31-1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sfixed32_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local msg = 'Field "val" of "sfixed32" type gets "string" type value.'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sfixed32_exception_type_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local msg = 'Input cdata value "ctype<float>" ' ..
        'for "val" field is not integer'
    local data = {val = ffi.cast('float', 0.5)}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end
