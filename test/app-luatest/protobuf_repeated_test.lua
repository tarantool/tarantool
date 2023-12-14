local t = require('luatest')
local protobuf = require('protobuf')
local g = t.group()

g.test_repeated_packed_int32 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated int32', 1}})
    })
    local result = protocol:encode('test', {val = {1, 2, 3, 4}})
    t.assert_equals(string.hex(result), '0a0401020304')
end

g.test_repeated_packed_fixed32 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated fixed32', 1}})
    })
    local data = {val = {1, 2, 3, 4}}
    local proto_res = '0a1001000000020000000300000004000000'
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), proto_res)
end

g.test_repeated_packed_float = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated float', 1}})
    })
    local data = {val = {0.5, 1.3, -15.5}}
    local proto_res = '0a0c0000003f6666a63f000078c1'
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), proto_res)
end

g.test_repeated_packed_len = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated bytes', 1}})
    })
    local result = protocol:encode('test', {val = {'fuz','buz'}})
    t.assert_equals(string.hex(result), '0a0366757a0a0362757a')
end

g.test_repeated_packed_message = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated field', 1}}),
        protobuf.message('field', {id = {'int32', 1}, name = {'string', 2}})
    })
    local data = {val = {{id = 1, name = 'fuz'}, {id = 2, name = 'buz'}}}
    local proto_res = '0a07120366757a08010a07120362757a0802'
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), proto_res)
end

g.test_repeated_packed_enum = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated field', 1}}),
        protobuf.enum('field', {True = 1, False = 0})
    })
    local data = {val = {'True', 'True', 'False'}}
    local proto_res = '080108010800'
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), proto_res)
end

g.test_repeated_exception_wrong_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated int32', 1}})
    })
    local msg = 'Field "val" of "int32" type gets "string" type value.'
    local data = {val = {'fuz', 'buz'}}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_repeated_exception_single_value = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated int32', 1}})
    })
    local msg = 'For repeated fields table data are needed'
    local data = {val = 12}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_repeated_exception_not_number_key = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated int32', 1}})
    })
    local msg = 'Input array for "val" repeated field contains ' ..
        'non-numeric key: "fuz"'
    local data = {val = {1, fuz = 2, 3}}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_repeated_exception_not_integer_key = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated int32', 1}})
    })
    local msg = 'Input array for "val" repeated field contains ' ..
        'non-integer numeric key: "0.5"'
    local data = {val = {1, [0.5] = 2, 3}}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_repeated_exception_incorrect_first_key = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated int32', 1}})
    })
    local msg = 'Input array for "val" repeated field got min index 2. ' ..
        'Must be 1'
    local data = {val = {[2] = 2, [3] = 3, [4] = 4}}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_repeated_exception_key_gap = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated int32', 1}})
    })
    local msg = 'Input array for "val" repeated field has ' ..
        'inconsistent keys. Got table with 3 fields and max index of 4'
    local data = {val = {[1] = 1, [3] = 3, [4] = 4}}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end
