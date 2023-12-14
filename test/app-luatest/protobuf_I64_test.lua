local t = require('luatest')
local protobuf = require('protobuf')
local g = t.group()

g.test_double = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 0.5})
    t.assert_equals(string.hex(result), '09000000000000e03f')
end

g.test_double_min_32 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = -3.4028234E+38})
    t.assert_equals(string.hex(result), '0934b886d5ffffefc7')
end

g.test_double_max_32 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 3.4028234E+38})
    t.assert_equals(string.hex(result), '0934b886d5ffffef47')
end

g.test_double_min_64 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local data = {val = -1.7976931348623157E+308}
    local result = protobuf.encode(protocol, 'test', data)
    t.assert_equals(string.hex(result), '09ffffffffffffefff')
end

g.test_double_max_64 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local data = {val = 1.7976931348623157E+308}
    local result = protobuf.encode(protocol, 'test', data)
    t.assert_equals(string.hex(result), '09ffffffffffffef7f')
end

g.test_double_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local msg = 'Field "val" of "double" type gets "string" type value. ' ..
        'Unsupported or colliding types'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_double_exception_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local msg = 'Field "val" of "double" type gets "cdata" type value. ' ..
        'Unsupported or colliding types'
    local data = {val = 10LL}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_fixed64 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 10})
    t.assert_equals(string.hex(result), '090a00000000000000')
end

g.test_fixed64_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 0})
    t.assert_equals(string.hex(result), '090000000000000000')
end

g.test_fixed64_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 10LL})
    t.assert_equals(string.hex(result), '090a00000000000000')
end

g.test_fixed64_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local data = {val = 18446744073709550591}
    local result = protobuf.encode(protocol, 'test', data)
    t.assert_equals(string.hex(result), '0900f8ffffffffffff')
end

g.test_fixed64_exception_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
      local msg = 'Input number value "1.844674407371e+19" for "val" field ' ..
          'does not fit in int64'
    local data = {val = 18446744073709561616}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_fixed64_exception_negative = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local msg = 'Input data for "val" field is -1 ' ..
        'and do not fit in unsigned type'
    local data = {val = -1}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_fixed64_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local msg = 'Field "val" of "fixed64" type gets "string" type value. ' ..
        'Unsupported or colliding types'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_sfixed64 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 10})
    t.assert_equals(string.hex(result), '090a00000000000000')
end

g.test_sfixed64_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 0})
    t.assert_equals(string.hex(result), '090000000000000000')
end

g.test_sfixed64_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 10LL})
    t.assert_equals(string.hex(result), '090a00000000000000')
end

g.test_sfixed64_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local data = {val = 9223372036854775295}
    local result = protobuf.encode(protocol, 'test', data)
    t.assert_equals(string.hex(result), '0900fcffffffffff7f')
end

g.test_sfixed64_min = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local data = {val = -9223372036854775808}
    local result = protobuf.encode(protocol, 'test', data)
    t.assert_equals(string.hex(result), '090000000000000080')
end

g.test_sfixed64_exception_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
   })
   local msg = 'Input data for "val" field is -9223372036854775808 ' ..
        'and do not fit in sint_64'
    local data = {val = 9823372036854775808}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_sfixed64_exception_negsize = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local msg = 'Input data for "val" field is -9223372036854775808 ' ..
        'and do not fit in sint_64'
    local data = {val = -9823372036854775809}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end

g.test_sfixed64_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local msg = ' Field "val" of "sfixed64" type gets "string" type value. ' ..
        'Unsupported or colliding types'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
end
