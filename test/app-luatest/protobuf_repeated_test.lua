local t = require('luatest')
local protobuf = require('protobuf')
local g = t.group()

local p = t.group('packed_repeated_encoding', {
    {type = 'repeated float', value = {0.5, 1.5},
        res = '0a080000003f0000c03f'},
    {type = 'repeated double', value = {0.5, 1.5},
        res = '0a10000000000000e03f000000000000f83f'},
    {type = 'repeated int32', value = {1, 2, 3, 4}, res = '0a0401020304'},
    {type = 'repeated sint32', value = {1, 2, 3, 4}, res = '0a0402040608'},
    {type = 'repeated uint32', value = {1, 2, 3, 4}, res = '0a0401020304'},
    {type = 'repeated int64', value = {1, 2, 3, 4}, res = '0a0401020304'},
    {type = 'repeated sint64', value = {1, 2, 3, 4}, res = '0a0402040608'},
    {type = 'repeated uint64', value = {1, 2, 3, 4}, res = '0a0401020304'},
    {type = 'repeated fixed32', value = {1, 2}, res = '0a080100000002000000'},
    {type = 'repeated sfixed32', value = {1, 2}, res = '0a080100000002000000'},
    {type = 'repeated fixed64', value = {1, 2},
        res = '0a1001000000000000000200000000000000'},
    {type = 'repeated sfixed64', value = {1, 2},
        res = '0a1001000000000000000200000000000000'},
    {type = 'repeated bool', value = {true, true}, res = '0a020101'},
})

p.test_packed_repeated_encoding = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local result = protocol:encode('test', {val = cg.params.value})
    t.assert_equals(string.hex(result), cg.params.res)
end

g.test_packed_repeated_int32_long_tag = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated int32', 1000}})
    })
    local result = protocol:encode('test', {val = {1, 2, 3, 4}})
    t.assert_equals(string.hex(result), 'c23e0401020304')
end

g.test_exception_repeated_int32_with_default_value = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated int32', 1}})
    })
    local msg = 'Input for "val" repeated field contains default value ' ..
        'can`t be encoded correctly'
    local data = {val = {1, 0, 0, 4}}
    t.assert_error_msg_contains(msg, protocol.encode,
        protocol, 'test', data)
end

local p = t.group('non_packed_repeated_encoding', {
    {type = 'repeated bytes', value = {'fuz', 'buz'},
        res = '0a0366757a0a0362757a'},
    {type = 'repeated string', value = {'fuz', 'buz'},
        res = '0a0366757a0a0362757a'},
})

p.test_non_packed_repeated_encoding = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local result = protocol:encode('test', {val = cg.params.value})
    t.assert_equals(string.hex(result), cg.params.res)
end

g.test_repeated_message = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated field', 1}}),
        protobuf.message('field', {id = {'int32', 1}, name = {'string', 2}})
    })
    local data = {val = {{id = 1, name = 'fuz'}, {id = 2, name = 'buz'}}}
    local proto_res = '0a07120366757a08010a07120362757a0802'
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), proto_res)
end

g.test_repeated_enum = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated field', 1}}),
        protobuf.enum('field', {Default = 0, True = 1, False = 2})
    })
    local data = {val = {'True', 'True', 'False'}}
    local proto_res = '080108010802'
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), proto_res)
end

local p = t.group('exceptions_repeated_encoding', {
    {value = {1, 'fuz'}, msg = 'Field "val" of "int32" type ' ..
        'gets "string" type value.'},
    {value = 12, msg = 'For repeated fields table data are needed'},
    {value = {1, fuz = 2, 3}, msg = 'Input array for "val" repeated ' ..
        'field contains non-numeric key: "fuz"'},
    {value = {1, [0.5] = 2, 3}, msg = 'Input array for "val" repeated ' ..
        'field contains non-integer numeric key: "0.5"'},
    {value = {[2] = 2, [3] = 3, [4] = 4}, msg = 'Input array for "val" ' ..
        'repeated field got min index 2. Must be 1'},
    {value = {[1] = 1, [3] = 3, [4] = 4}, msg = 'Input array for "val" ' ..
        'repeated field has inconsistent keys. Got table with 3 fields ' ..
        'and max index of 4'},
    {value = {1, nil, 2}, msg  = 'Input array for "val" repeated field ' ..
        'has inconsistent keys. Got table with 2 fields and max index of 3'},
    {value = {1, box.NULL, 2}, msg = 'Input array for "val" repeated ' ..
        'field contains box.NULL value which leads to ambiguous behaviour'},
})

p.test_exceptions_repeated_encoding = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'repeated int32', 1}})
    })
    local data = {val = cg.params.value}
    t.assert_error_msg_contains(cg.params.msg, protocol.encode,
        protocol, 'test', data)
end
