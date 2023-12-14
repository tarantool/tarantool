local t = require('luatest')
local protobuf = require('protobuf')
local g = t.group()

g.test_float = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 0.5})
    t.assert_equals(string.hex(result), '0d0000003f')
end

g.test_float_min = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = -3.4028234E+38})
    t.assert_equals(string.hex(result), '0dffff7fff')
end

g.test_float_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 3.4028234E+38})
    t.assert_equals(string.hex(result), '0dffff7f7f')
end

g.test_float_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local msg = 'Field "val" of "float" type gets "string" type value. ' ..
        'Unsupported or colliding types'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_float_exception_type_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'float', 1}})
    })
    local msg = 'Field "val" of "float" type gets "cdata" type value. ' ..
        'Unsupported or colliding types'
    local data = {val = 15LL}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_fixed32_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 0})
    t.assert_equals(string.hex(result), '0d00000000')
end

g.test_fixed32_number = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 10})
    t.assert_equals(string.hex(result), '0d0a000000')
end

g.test_fixed32_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 10ULL})
    t.assert_equals(string.hex(result), '0d0a000000')
end

g.test_fixed32_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 4294967295})
    t.assert_equals(string.hex(result), '0dffffffff')
end

g.test_fixed32_max_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 4294967295LL})
    t.assert_equals(string.hex(result), '0dffffffff')
end

g.test_fixed32_exception_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local msg = 'Input number value "4294967296" for "val" field ' ..
        'does not fit in int32'
    local data = {val = 4294967296}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_fixed32_exception_size_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local msg = 'Input data for "val" field is 4294967296ULL and ' ..
        'do not fit in int_32'
    local data = {val = 4294967296ULL}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_fixed32_exception_sign = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local msg = 'Input data for "val" field is -1 ' ..
        'and do not fit in unsigned type'
    local data = {val = -1}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_fixed32_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed32', 1}})
    })
    local msg = 'Field "val" of "fixed32" type gets "string" type value. ' ..
        'Unsupported or colliding types'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_sfixed32_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 0})
    t.assert_equals(string.hex(result), '0d00000000')
end

g.test_sfixed32 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 10})
    t.assert_equals(string.hex(result), '0d0a000000')
end

g.test_sfixed32_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 10ULL})
    t.assert_equals(string.hex(result), '0d0a000000')
end

g.test_sfixed32_min = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = -2147483648})
    t.assert_equals(string.hex(result), '0d00000080')
end

g.test_sfixed32_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 2147483647})
    t.assert_equals(string.hex(result), '0dffffff7f')
end

g.test_sfixed32_cdata_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 2147483648ULL})
    t.assert_equals(string.hex(result), '0d00000080')
end

g.test_sfixed32_exception_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local msg = 'Input data for "val" field is 2147483648 ' ..
        'and do not fit in int_32'
    local data = {val = 2147483648}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_sfixed32_exception_neg_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local msg = 'Input data for "val" field is -2147483649 ' ..
        'and do not fit in int_32'
    local data = {val = -2147483649}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_sfixed32_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed32', 1}})
    })
    local msg = 'Field "val" of "sfixed32" type gets "string" type value. ' ..
        'Unsupported or colliding types'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end
