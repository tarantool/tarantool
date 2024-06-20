local t = require('luatest')
local protobuf = require('protobuf')
local g = t.group()

g.test_double = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local result = protocol:encode('test', {val = 0.5})
    t.assert_equals(string.hex(result), '09000000000000e03f')
end

g.test_double_min_32 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local result = protocol:encode('test', {val = -0x1.fffffep+127})
    t.assert_equals(string.hex(result), '09000000e0ffffefc7')
end

g.test_double_max_32 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local result = protocol:encode('test', {val = 0x1.fffffep+127})
    t.assert_equals(string.hex(result), '09000000e0ffffef47')
end

g.test_double_min_64 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local result = protocol:encode('test', {val = -0x1.fffffffffffffp+1023})
    t.assert_equals(string.hex(result), '09ffffffffffffefff')
end

g.test_double_max_64 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local result = protocol:encode('test', {val = 0x1.fffffffffffffp+1023})
    t.assert_equals(string.hex(result), '09ffffffffffffef7f')
end

g.test_double_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local msg = 'Field "val" of "double" type gets "string" type value.'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_double_exception_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local msg = 'Field "val" of "double" type gets "cdata" type value.'
    local data = {val = 10LL}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_double_exception_inf = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'double', 1}})
    })
    local msg = 'Input data for "val" field is inf'
    local data = {val = 1/0}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_fixed64 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local result = protocol:encode('test', {val = 10})
    t.assert_equals(string.hex(result), '090a00000000000000')
end

g.test_fixed64_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local result = protocol:encode('test', {val = 0})
    t.assert_equals(string.hex(result), '090000000000000000')
end

g.test_fixed64_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local result = protocol:encode('test', {val = 10ULL})
    t.assert_equals(string.hex(result), '090a00000000000000')
end

g.test_fixed64_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local data = {val = 2^64-1025}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '0900f8ffffffffffff')
end

g.test_fixed64_exception_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
      local msg = 'Input data for "val" field is "1.844674407371e+19" and ' ..
          'do not fit in "fixed64"'
    local data = {val = 2^64}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_fixed64_exception_not_integral = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local msg = 'Input number value 10.500000 for "val" is not integer'
    local data = {val = 10.5}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_fixed64_exception_negative = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local msg = 'Input data for "val" field is "-1" and ' ..
        'do not fit in "fixed64"'
    local data = {val = -1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_fixed64_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'fixed64', 1}})
    })
    local msg = 'Field "val" of "fixed64" type gets "string" type value.'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sfixed64 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local result = protocol:encode('test', {val = 10})
    t.assert_equals(string.hex(result), '090a00000000000000')
end

g.test_sfixed64_zero = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local result = protocol:encode('test', {val = 0})
    t.assert_equals(string.hex(result), '090000000000000000')
end

g.test_sfixed64_cdata = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local result = protocol:encode('test', {val = 10LL})
    t.assert_equals(string.hex(result), '090a00000000000000')
end

g.test_sfixed64_max = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local data = {val = 2^63-513}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '0900fcffffffffff7f')
end

g.test_sfixed64_min = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local data = {val = -2^63}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '090000000000000080')
end

g.test_sfixed64_exception_size = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
   })
   local msg = 'Input data for "val" field is "9.2233720368548e+18" ' ..
        'and do not fit in "sfixed64"'
    local data = {val = 2^63}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sfixed64_exception_not_integral = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local msg = 'Input number value 10.500000 for "val" is not integer'
    local data = {val = 10.5}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sfixed64_exception_negsize = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local msg = 'Input data for "val" field is "-9.2233720368548e+18" ' ..
        'and do not fit in "sfixed64"'
    local data = {val = -2^63-1025}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_sfixed64_exception_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'sfixed64', 1}})
    })
    local msg = ' Field "val" of "sfixed64" type gets "string" type value.'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end
