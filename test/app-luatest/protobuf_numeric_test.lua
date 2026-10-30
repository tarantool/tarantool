local ffi = require('ffi')
local t = require('luatest')
local protobuf = require('protobuf')

local p = t.group('upper_limit', {
    {type = 'int32', value = 2^31 - 1, res = '08ffffffff07'},
    {type = 'int32', value = 2LL^31 - 1, res = '08ffffffff07'},
    {type = 'int32', value = 2ULL^31 - 1, res = '08ffffffff07'},
    {type = 'sint32', value = 2^31 - 1, res = '08feffffff0f'},
    {type = 'sint32', value = 2LL^31 - 1, res = '08feffffff0f'},
    {type = 'sint32', value = 2ULL^31 - 1, res = '08feffffff0f'},
    {type = 'uint32', value = 2^32 - 1, res = '08ffffffff0f'},
    {type = 'uint32', value = 2LL^32 - 1, res = '08ffffffff0f'},
    {type = 'uint32', value = 2ULL^32 - 1, res = '08ffffffff0f'},
    {type = 'int64', value = 2^63 - 513, res = '0880f8ffffffffffff7f'},
    {type = 'int64', value = 2LL^63 - 1, res = '08ffffffffffffffff7f'},
    {type = 'int64', value = 2ULL^63 - 1, res = '08ffffffffffffffff7f'},
    {type = 'sint64', value = 2^63 - 513, res = '0880f0ffffffffffffff01'},
    {type = 'sint64', value = 2LL^63 - 1, res = '08feffffffffffffffff01'},
    {type = 'sint64', value = 2ULL^63 - 1, res = '08feffffffffffffffff01'},
    {type = 'uint64', value = 2^64 - 1025, res = '0880f0ffffffffffffff01'},
    {type = 'uint64', value = 2ULL^64 - 1, res = '08ffffffffffffffffff01'},
    {type = 'bool', value = true, res = '0801'},
    {type = 'float', value = 0x1.fffffep+127, res = '0dffff7f7f'},
    {type = 'double', value = 0x1.fffffffffffffp+1023,
        res = '09ffffffffffffef7f'},
    {type = 'fixed32', value = 2^32 - 1, res = '0dffffffff'},
    {type = 'fixed32', value = 2LL^32 - 1, res = '0dffffffff'},
    {type = 'fixed32', value = 2ULL^32 - 1, res = '0dffffffff'},
    {type = 'sfixed32', value = 2^31 - 1, res = '0dffffff7f'},
    {type = 'sfixed32', value = 2LL^31 - 1, res = '0dffffff7f'},
    {type = 'sfixed32', value = 2ULL^31 - 1, res = '0dffffff7f'},
    {type = 'fixed64', value = 2^64 - 1025, res = '0900f8ffffffffffff'},
    {type = 'fixed64', value = 2ULL^64 - 1, res = '09ffffffffffffffff'},
    {type = 'sfixed64', value = 2^63 - 513, res = '0900fcffffffffff7f'},
    {type = 'sfixed64', value = 2LL^63 - 1, res = '09ffffffffffffff7f'},
    {type = 'sfixed64', value = 2ULL^63 - 1, res = '09ffffffffffffff7f'},

})

p.test_upper_limit = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local result = protocol:encode('test', {val = cg.params.value})
    t.assert_equals(string.hex(result), cg.params.res)
end

p = t.group('lower_limit', {
    {type = 'int32', value = -2^31, res = '0880808080f8ffffffff01'},
    {type = 'int32', value = -2LL^31, res = '0880808080f8ffffffff01'},
    {type = 'sint32', value = -2^31, res = '08ffffffff0f'},
    {type = 'sint32', value = -2LL^31, res = '08ffffffff0f'},
    {type = 'uint32', value = 0, res = ''},
    {type = 'uint32', value = 0LL, res = ''},
    {type = 'uint32', value = 0ULL, res = ''},
    {type = 'int64', value = -2^63, res = '0880808080808080808001'},
    {type = 'int64', value = -2LL^63, res = '0880808080808080808001'},
    {type = 'sint64', value = -2^63, res = '08ffffffffffffffffff01'},
    {type = 'sint64', value = -2LL^63, res = '08ffffffffffffffffff01'},
    {type = 'uint64', value = 0, res = ''},
    {type = 'uint64', value = 0LL, res = ''},
    {type = 'uint64', value = 0ULL, res = ''},
    {type = 'bool', value = false, res = ''},
    {type = 'float', value = -0x1.fffffep+127, res = '0dffff7fff'},
    {type = 'double', value = -0x1.fffffffffffffp+1023,
        res = '09ffffffffffffefff'},
    {type = 'fixed32', value = 0, res = ''},
    {type = 'fixed32', value = 0LL, res = ''},
    {type = 'fixed32', value = 0ULL, res = ''},
    {type = 'sfixed32', value = -2^31, res = '0d00000080'},
    {type = 'sfixed32', value = -2LL^31, res = '0d00000080'},
    {type = 'fixed64', value = 0, res = ''},
    {type = 'fixed64', value = 0LL, res = ''},
    {type = 'fixed64', value = 0ULL, res = ''},
    {type = 'sfixed64', value = -2^63, res = '090000000000000080'},
    {type = 'sfixed64', value = -2LL^63, res = '090000000000000080'},
})

p.test_lower_limit = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local result = protocol:encode('test', {val = cg.params.value})
    t.assert_equals(string.hex(result), cg.params.res)
end

p = t.group('exception_input_data_float', t.helpers.matrix({
    type = {'int32', 'sint32', 'uint32', 'int64', 'sint64', 'uint64',
        'fixed32', 'sfixed32', 'fixed64', 'sfixed64'},
    arg = {
        {value = 1.5, msg = 'Input number value 1.500000 for ' ..
            '"val" is not integer'},
        {value = ffi.cast('float', 1.5), msg = 'Input cdata value ' ..
            '"ctype<float>" for "val" field is not integer'},
    }
}))

p.test_exception_input_data_float = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local data = {val = cg.params.arg.value}
    t.assert_error_msg_contains(cg.params.arg.msg, protocol.encode,
        protocol, 'test', data)
end

p = t.group('exception_input_data_wrong_type', t.helpers.matrix({
    type = {'int32', 'sint32', 'uint32', 'int64', 'sint64', 'uint64',
        'fixed32', 'sfixed32', 'fixed64', 'sfixed64', 'bool', 'float',
        'double'},
}))

p.test_exception_input_data_wrong_type = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local msg = 'Field "val" of "' .. cg.params.type .. '" type gets ' ..
        '"string" type value.'
    local data = {val = 'str'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

p = t.group('exception_cdata_input_for_float_field', t.helpers.matrix({
    type = {'float', 'double'},
}))

p.test_exception_cdata_input_for_float_field = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local msg = 'Field "val" of "' .. cg.params.type .. '" type gets ' ..
        '"cdata" type value.'
    local data = {val = ffi.cast('float', 1.5)}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

p = t.group('exception_input_data_Nan', t.helpers.matrix({
    type = {'int32', 'sint32', 'uint32', 'int64', 'sint64', 'uint64',
        'fixed32', 'sfixed32', 'fixed64', 'sfixed64', 'float', 'double'},
    arg = {
        {value = 0/0, msg = 'Input data for "val" field is NaN'},
        {value = 1/0, msg = 'Input data for "val" field is inf'},
        {value = -1/0, msg = 'Input data for "val" field is inf'},
    }
}))

p.test_exception_input_data_Nan = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local data = {val = cg.params.arg.value}
    t.assert_error_msg_contains(cg.params.arg.msg, protocol.encode,
        protocol, 'test', data)
end

local p = t.group('exception_input_data_out_of_range', {
    {type = 'int32', value = 2^31, res = '2147483648'},
    {type = 'int32', value = 2LL^31, res = '2147483648LL'},
    {type = 'int32', value = 2ULL^31, res = '2147483648ULL'},
    {type = 'int32', value = -2^31 - 1, res = '-2147483649'},
    {type = 'int32', value = -2LL^31 - 1, res = '-2147483649LL'},
    {type = 'sint32', value = 2^31, res = '2147483648'},
    {type = 'sint32', value = 2LL^31, res = '2147483648LL'},
    {type = 'sint32', value = 2ULL^31, res = '2147483648ULL'},
    {type = 'sint32', value = -2^31 - 1, res = '-2147483649'},
    {type = 'sint32', value = -2LL^31 - 1, res = '-2147483649LL'},
    {type = 'uint32', value = 2^32, res = '4294967296'},
    {type = 'uint32', value = 2LL^32, res = '4294967296LL'},
    {type = 'uint32', value = 2ULL^32, res = '4294967296ULL'},
    {type = 'uint32', value = -1, res = '-1'},
    {type = 'uint32', value = -1LL, res = '-1LL'},
    {type = 'int64', value = 2^63 - 512, res = '9.2233720368548e+18'},
    {type = 'int64', value = 2ULL^63, res = '9223372036854775808ULL'},
    {type = 'int64', value = -2^63 - 1025, res = '-9.2233720368548e+18'},
    {type = 'sint64', value = 2^63 - 512, res = '9.2233720368548e+18'},
    {type = 'sint64', value = 2ULL^63, res = '9223372036854775808ULL'},
    {type = 'sint64', value = -2^63 - 1025, res = '-9.2233720368548e+18'},
    {type = 'uint64', value = -1, res = '-1'},
    {type = 'uint64', value = -1LL, res = '-1LL'},
    {type = 'float', value = 0x1.fffffe018d3f8p+127, res = '3.402823467e+38'},
    {type = 'fixed32', value = 2^32, res = '4294967296'},
    {type = 'fixed32', value = 2LL^32, res = '4294967296LL'},
    {type = 'fixed32', value = 2ULL^32, res = '4294967296ULL'},
    {type = 'fixed32', value = -1, res = '-1'},
    {type = 'fixed32', value = -1LL, res = '-1LL'},
    {type = 'sfixed32', value = 2^31, res = '2147483648'},
    {type = 'sfixed32', value = 2LL^31, res = '2147483648LL'},
    {type = 'sfixed32', value = 2ULL^31, res = '2147483648ULL'},
    {type = 'sfixed32', value = -2^31 - 1, res = '-2147483649'},
    {type = 'sfixed32', value = -2LL^31 - 1, res = '-2147483649LL'},
    {type = 'fixed64', value = 2^64, res = '1.844674407371e+19'},
    {type = 'fixed64', value = -1, res = '-1'},
    {type = 'fixed64', value = -1LL, res = '-1LL'},
    {type = 'sfixed64', value = 2^63 - 512, res = '9.2233720368548e+18'},
    {type = 'sfixed64', value = 2ULL^63, res = '9223372036854775808ULL'},
    {type = 'sfixed64', value = -2^63 - 1025, res = '-9.2233720368548e+18'},
})

p.test_exception_input_data_out_of_range = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local msg = 'Input data for "val" field is "' .. cg.params.res..
        '" and do not fit in "' .. cg.params.type .. '"'
    local data = {val = cg.params.value}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

p = t.group('regular_signed_values', t.helpers.matrix({
    value = {1540, -770, -10LL, 10ULL},
    res = {
        {type = 'int32', code = {['1540'] = '08840c',
            ['-770'] = '08fef9ffffffffffffff01', ['10ULL'] = '080a',
            ['-10LL'] = '08f6ffffffffffffffff01'}},
        {type = 'sint32', code = {['1540'] = '088818', ['-770'] = '08830c',
            ['10ULL'] = '0814', ['-10LL'] = '0813'}},
        {type = 'int64', code = {['1540'] = '08840c',
            ['-770'] = '08fef9ffffffffffffff01', ['10ULL'] = '080a',
            ['-10LL'] = '08f6ffffffffffffffff01'}},
        {type = 'sint64', code = {['1540'] = '088818', ['-770'] = '08830c',
            ['10ULL'] = '0814', ['-10LL'] = '0813'}},
        {type = 'sfixed32', code = {['1540'] = '0d04060000',
            ['-770'] = '0dfefcffff', ['10ULL'] = '0d0a000000',
            ['-10LL'] = '0df6ffffff'}},
        {type = 'sfixed64', code = {['1540'] = '090406000000000000',
            ['-770'] = '09fefcffffffffffff', ['10ULL'] = '090a00000000000000',
            ['-10LL'] = '09f6ffffffffffffff'}},
    },
}))

p.test_regular_signed_values = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.res.type, 1}})
    })
    local result = protocol:encode('test', {val = cg.params.value})
    t.assert_equals(string.hex(result),
        cg.params.res.code[tostring(cg.params.value)])
end

p = t.group('regular_usigned_values', t.helpers.matrix({
    value = {1540, 10LL, 15ULL},
    res = {
        {type = 'uint32', code = {['1540'] = '08840c', ['10LL'] = '080a',
            ['15ULL'] = '080f'}},
        {type = 'uint64', code = {['1540'] = '08840c', ['10LL'] = '080a',
            ['15ULL'] = '080f'}},
        {type = 'fixed32', code = {['1540'] = '0d04060000',
            ['10LL'] = '0d0a000000', ['15ULL'] = '0d0f000000'}},
        {type = 'fixed64', code = {['1540'] = '090406000000000000',
            ['10LL'] = '090a00000000000000',
            ['15ULL'] = '090f00000000000000'}},
    },
}))

p.test_regular_unsigned_values = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.res.type, 1}})
    })
    local result = protocol:encode('test', {val = cg.params.value})
    t.assert_equals(string.hex(result),
        cg.params.res.code[tostring(cg.params.value)])
end

p = t.group('regular_floating_point_values', t.helpers.matrix({
    value = {1.5, -1.5},
    res = {
        {type = 'float', code = {['1.5'] = '0d0000c03f',
            ['-1.5'] = '0d0000c0bf'}},
        {type = 'double', code = {['1.5'] = '09000000000000f83f',
            ['-1.5'] = '09000000000000f8bf'}},
    },
}))

p.test_regular_floating_point_values = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.res.type, 1}})
    })
    local result = protocol:encode('test', {val = cg.params.value})
    t.assert_equals(string.hex(result),
        cg.params.res.code[tostring(cg.params.value)])
end

p = t.group('numeric_types_default_value_encoding', t.helpers.matrix({
    type = {'int32', 'sint32', 'uint32', 'int64', 'sint64', 'uint64',
        'fixed32', 'sfixed32', 'fixed64', 'sfixed64', 'float', 'double'},
    value = {0}
}))

p.test_numeric_types_default_value_encoding = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local result = protocol:encode('test', {val = cg.params.value})
    t.assert_equals(string.hex(result), '')
end

p = t.group('other_types_default_value_encoding', {
    {type = 'bool', value = false},
    {type = 'string', value = ''},
    {type = 'bytes', value = ''},
})

p.test_numeric_types_default_value_encoding = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local result = protocol:encode('test', {val = cg.params.value})
    t.assert_equals(string.hex(result), '')
end
