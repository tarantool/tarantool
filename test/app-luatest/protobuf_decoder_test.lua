local ffi = require('ffi')
local t = require('luatest')
local protobuf = require('protobuf')
local g = t.group()

local p = t.group('numeric_type_decoding', {
    {type = 'int32', value = '\b\n', res = {val = 10}},
    {type = 'sint32', value = '\b\x14', res = {val = 10}},
    {type = 'uint32', value = '\b\n', res = {val = 10}},
    {type = 'int64', value = '\b\n', res = {val = 10}},
    {type = 'sint64', value = '\b\x14', res = {val = 10}},
    {type = 'uint64', value = '\b\n', res = {val = 10}},
    {type = 'bool', value = '\b\x01', res = {val = true}},
    {type = 'float', value = '\r\0\0\0?', res = {val = 0.5}},
    {type = 'double', value = '\t333333ӿ', res = {val = -0.3}},
    {type = 'fixed32', value = '\r\n\0\0\0', res = {val = 10}},
    {type = 'sfixed32', value = '\r\n\0\0\0', res = {val = 10}},
    {type = 'fixed64', value = '\t\n\0\0\0\0\0\0\0', res = {val = 10}},
    {type = 'sfixed64', value = '\t\n\0\0\0\0\0\0\0', res = {val = 10}},
    {type = 'string', value = '\n\x04abcd', res = {val = 'abcd'}},
    {type = 'bytes', value = '\n\x04abcd', res = {val = 'abcd'}},
})

p.test_numeric_type_decoding = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.type, 1}})
    })
    local result = protocol:decode('test', cg.params.value)
    t.assert_equals(result, cg.params.res)
    t.assert_equals(type(result.val), type(cg.params.res.val))
end

g.test_minimum_value_decoding = function()
    local testcases = {
        {type = 'int32', value = '\b\x80\x80\x80\x80\xF8\xFF\xFF\xFF\xFF\x01',
            res = {val = -2^31}},
        {type = 'sint32', value = '\b\xFF\xFF\xFF\xFF\x0F',
            res = {val = -2^31}},
        {type = 'uint32', value = '\b\1', res = {val = 1}},
        {type = 'int64', value = '\b\x80\x80\x80\x80\x80\x80\x80\x80\x80\x01',
            res = {val = -2LL^63}},
        {type = 'sint64', value = '\b\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01',
            res = {val = -2LL^63}},
        {type = 'uint64', value = '\b\1', res = {val = 1}},
        {type = 'float', value = '\r\xFF\xFF\x7F\xFF',
            res = {val = -0x1.fffffep+127}},
        {type = 'double', value = '\t\xFF\xFF\xFF\xFF\xFF\xFF\xEF\xFF',
            res = {val = -0x1.fffffffffffffp+1023}},
        {type = 'fixed32', value = '\r\x01\0\0\0', res = {val = 1}},
        {type = 'sfixed32', value = '\r\0\0\0\x80', res = {val = -2^31}},
        {type = 'fixed64', value = '\t\x01\0\0\0\0\0\0\0', res = {val = 1}},
        {type = 'sfixed64', value = '\t\0\0\0\0\0\0\0\x80',
            res = {val = -2LL^63}},
    }
    for _, test in pairs(testcases) do
        local protocol = protobuf.protocol({
            protobuf.message('test', {val = {test.type, 1}})
        })
        local result = protocol:decode('test', test.value)
        t.assert_equals(result, test.res)
        t.assert_equals(type(result.val), type(test.res.val))
        if type(test.res.val) == 'cdata' then
            t.assert_equals(ffi.typeof(result.val), ffi.typeof(test.res.val))
        end
    end
end

g.test_maximum_value_decoding = function()
    local testcases = {
        {type = 'int32', value = '\b\xFF\xFF\xFF\xFF\a',
            res = {val = 2^31 - 1}},
        {type = 'sint32', value = '\b\xFE\xFF\xFF\xFF\x0F',
            res = {val = 2^31 - 1}},
        {type = 'uint32', value = '\b\xFF\xFF\xFF\xFF\x0F',
            res = {val = 2^32 - 1}},
        {type = 'int64', value = '\b\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F',
            res = {val = 2LL^63 - 1}},
        {type = 'sint64', value = '\b\xFE\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01',
            res = {val = 2LL^63 - 1}},
        {type = 'uint64', value = '\b\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01',
            res = {val = 2ULL^64 - 1}},
        {type = 'bool', value = '\b\x01', res = {val = true}},
        {type = 'float', value = '\r\xFF\xFF\x7F\x7F',
            res = {val = 0x1.fffffep+127}},
        {type = 'double', value = '\t\xFF\xFF\xFF\xFF\xFF\xFF\xEF\x7F',
            res = {val = 0x1.fffffffffffffp+1023}},
        {type = 'fixed32', value = '\r\xFF\xFF\xFF\xFF',
            res = {val = 2^32 - 1}},
        {type = 'sfixed32', value = '\r\xFF\xFF\xFF\x7F',
            res = {val = 2^31 - 1}},
        {type = 'fixed64', value = '\t\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF',
            res = {val = 2ULL^64 - 1}},
        {type = 'sfixed64', value = '\t\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x7F',
            res = {val = 2LL^63 - 1}},
    }
    for _, test in pairs(testcases) do
        local protocol = protobuf.protocol({
            protobuf.message('test', {val = {test.type, 1}})
        })
        local result = protocol:decode('test', test.value)
        t.assert_equals(result, test.res)
        t.assert_equals(type(result.val), type(test.res.val))
        if type(test.res.val) == 'cdata' then
            t.assert_equals(ffi.typeof(result.val), ffi.typeof(test.res.val))
        end
    end
end

g.test_repeated_field_decoding = function()
    local testcases = {
        {type = 'int32', value = '\n\x03\n\v\f', res = {val = {10, 11, 12}}},
        {type = 'sint32', value = '\n\x03\x14\x16\x18',
            res = {val = {10, 11, 12}}},
        {type = 'uint32', value = '\n\x03\n\v\f', res = {val = {10, 11, 12}}},
        {type = 'int64', value = '\n\x03\n\v\f', res = {val = {10, 11, 12}}},
        {type = 'sint64', value = '\n\x03\x14\x16\x18',
            res = {val = {10, 11, 12}}},
        {type = 'uint64', value = '\n\x03\n\v\f', res = {val = {10, 11, 12}}},
        {type = 'fixed32', value = '\n\f\n\0\0\0\v\0\0\0\f\0\0\0',
            res = {val = {10, 11, 12}}},
        {type = 'sfixed32', value = '\n\f\n\0\0\0\v\0\0\0\f\0\0\0',
            res = {val = {10, 11, 12}}},
        {type = 'fixed64', value = '\n\x10\n\0\0\0\0\0\0\0\v\0\0\0\0\0\0\0',
            res = {val = {10, 11}}},
        {type = 'sfixed64', value = '\n\x10\n\0\0\0\0\0\0\0\v\0\0\0\0\0\0\0',
            res = {val = {10, 11}}},
        {type = 'bool', value = '\n\x03\x01\x01\x01',
            res = {val = {true, true, true}}},
        {type = 'string', value = '\n\x03fuz\n\x03buz',
            res = {val = {'fuz', 'buz'}}},
        {type = 'bytes', value = '\n\x03fuz\n\x03buz',
            res = {val = {'fuz', 'buz'}}},
        {type = 'float', value = '\n\b\0\0\xC0?\0\0 @',
            res = {val = {1.5, 2.5}}},
        {type = 'double', value = '\n\x10\0\0\0\0\0\0\xF8?\0\0\0\0\0\0\x04@',
            res = {val = {1.5, 2.5}}},
    }
    for _, test in pairs(testcases) do
        local protocol = protobuf.protocol({
            protobuf.message('test', {val = {'repeated ' .. test.type, 1}})
        })
        local result = protocol:decode('test', test.value)
        t.assert_equals(result, test.res)
    end
end

g.test_repeated_field_decoding_from_unpacked = function()
    local testcases = {
        {type = 'repeated int32', value = '\b\x01\b\x02\b\x03',
            res = {val1 = {1, 2, 3}}},
        {type = 'repeated bool', value = '\b\x01\b\x01',
            res = {val1 = {true, true}}},
        {type = 'repeated int64', value = '\b\n\b\v\b\f',
            res = {val1 = {10, 11, 12}}}
    }
    for _, test in pairs(testcases) do
        local protocol = protobuf.protocol({
            protobuf.message('test', {val1 = {test.type, 1}})
        })
        local result = protocol:decode('test', test.value)
        t.assert_equals(result, test.res)
    end
end

g.test_decode_unpacked_repeated_field_out_of_order = function()
    local protocol = protobuf.protocol({
        protobuf.message('test_message', {
            val1 = {'int32', 1},
            val2 = {'repeated int32', 2},
        })
    })
    local encoded_msg = '\x10\v\b\n\x10\f\x10\r'
    local expected_res = {val1 = 10, val2 = {11, 12, 13}}
    local result = protocol:decode('test_message', encoded_msg)
    t.assert_equals(result, expected_res)
end

g.test_repeated_enum_decoding = function()
    local protocol = protobuf.protocol({
        protobuf.message('test_message', {
            val1 = {'repeated test_enum', 1},
        }),
        protobuf.enum('test_enum', {
            ok = 0,
            err1 = 1,
            err2 = 2,
        })
    })
    local encoded_msg = '\b\x01\b\x02\b\x03'
    local expected_res = {val1 = {'err1', 'err2', 3}}
    local result = protocol:decode('test_message', encoded_msg)
    t.assert_equals(result, expected_res)
end

g.test_repeated_message_decoding = function()
    local protocol = protobuf.protocol({
        protobuf.message('first_message', {
            val1 = {'repeated second_message', 1},
        }),
        protobuf.message('second_message', {
            val1 = {'int32', 1},
            val2 = {'repeated string', 2},
        })
    })
    local encoded_msg = '\n\f\x12\x03fuz\x12\x03buz\b\x01\n\f\x12\x03buz' ..
        '\x12\x03fuz\b\x02'
    local expected_res = {val1 = {{val1 = 1, val2 = {"fuz", "buz"}},
        {val1 = 2, val2 = {"buz", "fuz"}}}}
    local result = protocol:decode('first_message', encoded_msg)
    t.assert_equals(result, expected_res)
end

local p = t.group('default value encoding', t.helpers.matrix({
    value = {''},
    arg = {
        {type = 'int32', res = {val = 0}},
        {type = 'sint32', res = {val = 0}},
        {type = 'uint32', res = {val = 0}},
        {type = 'int64', res = {val = 0}},
        {type = 'sint64', res = {val = 0}},
        {type = 'uint64', res = {val = 0}},
        {type = 'bool', res = {val = false}},
        {type = 'float', res = {val = 0}},
        {type = 'double', res = {val = 0}},
    }
}))

p.test_default_value_encoding = function(cg)
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {cg.params.arg.type, 1}})
    })
    local result = protocol:decode('test', cg.params.value,
        {set_default = true})
    t.assert_equals(result, cg.params.arg.res)
end

g.test_empty_input_set_default_false = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protocol:decode('test', '')
    t.assert_equals(result, {})
end

g.test_decode_embedded_message = function()
    local protocol = protobuf.protocol({
        protobuf.message('first_message', {
            val1 = {'int32', 1},
            val2 = {'second_message', 2},
        }),
        protobuf.message('second_message', {
            val1 = {'int32', 1},
            val2 = {'string', 2},
            val3 = {'fixed64', 3},
        })
    })
    local encoded_msg = '\x12\x13\x19\x05\0\0\0\0\0\0\0\b\f\x12\x06fuzbuz' ..
        '\b\xF6\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01'
    local expected_res = {val1 = -10,
        val2 = {val1 = 12, val2 = 'fuzbuz', val3 = 5}}
    local result = protocol:decode('first_message', encoded_msg)
    t.assert_equals(result, expected_res)
end

g.test_decode_embedded_message_with_set_default = function()
    local protocol = protobuf.protocol({
        protobuf.message('first_message', {
            val1 = {'int32', 1},
            val2 = {'second_message', 2},
        }),
        protobuf.message('second_message', {
            val1 = {'int32', 1},
            val2 = {'string', 2},
            val3 = {'fixed64', 3},
        })
    })
    local encoded_msg = '\x12\a\x12\x03xyz\b\n'
    local expected_res = {val1 = 0,
        val2 = {val1 = 10, val2 = 'xyz', val3 = 0}}
    local result = protocol:decode('first_message', encoded_msg,
        {set_default = true})
    t.assert_equals(result, expected_res)
end

g.test_decode_embedded_resursive_message_with_set_default = function()
    local protocol = protobuf.protocol({
        protobuf.message('first_message', {
            val1 = {'int32', 1},
            val2 = {'first_message', 2},
            val3 = {'int32', 3},
        })
    })
    local encoded_msg = '\x12\x04\x18\f\b\v\b\n'
    local expected_res = {val1 = 10, val2 = {val1 = 11, val3 = 12}, val3 = 0}
    local result = protocol:decode('first_message', encoded_msg,
        {set_default = true})
    t.assert_equals(result, expected_res)
end

g.test_decode_enum_with_set_default = function()
    local protocol = protobuf.protocol({
        protobuf.message('test_message', {
            val1 = {'int32', 1},
            val2 = {'test_enum', 2},
        }),
        protobuf.enum('test_enum', {
            ok = 0,
            err1 = 1,
        })
    })
    local encoded_msg = '\b\n'
    local expected_res = {val1 = 10, val2 = 'ok'}
    local result = protocol:decode('test_message', encoded_msg,
        {set_default = true})
    t.assert_equals(result, expected_res)
end

g.test_decode_enum = function()
    local protocol = protobuf.protocol({
        protobuf.message('test_message', {
            val1 = {'int32', 1},
            val2 = {'test_enum', 2},
        }),
        protobuf.enum('test_enum', {
            ok = 0,
            err = 1,
        })
    })
    local encoded_msg = '\x10\x01\b\n'
    local expected_res = {val1 = 10, val2 = 'err'}
    local result = protocol:decode('test_message', encoded_msg)
    t.assert_equals(result, expected_res)
end

g.test_decode_unknown_field_varint_and_len = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            val3 = {'fixed32', 3},
            val4 = {'fixed64', 4},
        })
    })
    local encoded_msg = '\x1D\n\0\x01\0\n\x03xyz!\x80\xF4\x01\0\0\0\0\0\x10\n'
    local expected_res = {_unknown_fields = {'\n\x03xyz', '\x10\n'},
        val3 = 65546, val4 = 128128}
    local result = protocol:decode('test', encoded_msg)
    t.assert_equals(result, expected_res)
end

g.test_decode_unknown_field_fixed32_and_fixed64 = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            val1 = {'bytes', 1},
            val2 = {'int64', 2},
        })
    })
    local encoded_msg = '\x1D\n\0\x01\0\n\x03xyz!\x80\xF4\x01\0\0\0\0\0\x10\n'
    local expected_res = {_unknown_fields = {'\x1D\n\0\x01\0',
        '!\x80\xF4\x01\0\0\0\0\0'}, val1 = "xyz", val2 = 10}
    local result = protocol:decode('test', encoded_msg)
    t.assert_equals(result, expected_res)
end

g.test_exception_decoded_varint_data_out_of_range = function()
    local testcases = {
        {type = 'int32', data = '\b\x80\x80\x80\x80\b', res = 2^31},
        {type = 'int32', data = '\b\xFF\xFF\xFF\xFF\xF7\xFF\xFF\xFF\xFF\x01',
            res = -2^31 - 1},
        {type = 'uint32', data = '\b\x80\x80\x80\x80\x10', res = 2^32},
        {type = 'sint32', data = '\b\x80\x80\x80\x80\x10', res = 2^31},
        {type = 'sint32', data = '\b\x81\x80\x80\x80\x10', res = -2^31 - 1},
    }
    for _, test in pairs(testcases) do
        local protocol = protobuf.protocol({
            protobuf.message('test', {val = {test.type, 1}})
        })
        local msg = 'Input data for "val" field is "' .. test.res ..
            '" and do not fit in "' .. test.type .. '"'
        t.assert_error_msg_contains(msg, protocol.decode, protocol,
            'test', test.data)
    end
end

g.test_exception_decoding_data_with_wrong_wire_type = function()
    local testcases = {
        {type = 'fixed32', encoded_wire_type = 'varint',
            expected_wire_type = 'I32', data = '\b\n'},
        {type = 'fixed32', encoded_wire_type = 'I64',
            expected_wire_type = 'I32', data = '\t\n\0\0\0\0\0\0\0'},
        {type = 'fixed32', encoded_wire_type = 'len',
            expected_wire_type = 'I32', data = '\n\x03fuz'},
        {type = 'string', encoded_wire_type = 'varint',
            expected_wire_type = 'len', data = '\b\n'},
        {type = 'string', encoded_wire_type = 'I64',
            expected_wire_type = 'len', data = '\t\n\0\0\0\0\0\0\0'},
        {type = 'string', encoded_wire_type = 'I32',
            expected_wire_type = 'len', data = '\r\n\0\0\0'},
        {type = 'fixed64', encoded_wire_type = 'varint',
            expected_wire_type = 'I64', data = '\b\n'},
        {type = 'fixed64', encoded_wire_type = 'I32',
            expected_wire_type = 'I64', data = '\r\n\0\0\0'},
        {type = 'fixed64', encoded_wire_type = 'len',
            expected_wire_type = 'I64', data = '\n\x03fuz'},
    }
    for _, test in pairs(testcases) do
        local protocol = protobuf.protocol({
            protobuf.message('test', {val = {test.type, 1}})
        })
        local msg = '[test.val] decoding error on position 2: Value for ' ..
            'field was encoded as "' .. test.encoded_wire_type .. '" and ' ..
            'can`t be decoded as "' .. test.expected_wire_type .. '"'
        t.assert_error_msg_contains(msg, protocol.decode, protocol,
            'test', test.data)
    end
end

g.test_exception_wrong_protobuf_type = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = '[test.val] decoding error on position 2: ' ..
        'Decoding data contains incorrect protobuf type: 4'
    local data = '\f\x01'
    t.assert_error_msg_contains(msg, protocol.decode, protocol, 'test', data)
end

g.test_exception_wrong_protocol_name = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'There is no message or enum named "xtest"' ..
        ' in the given protocol'
    local data = '\b\n'
    t.assert_error_msg_contains(msg, protocol.decode, protocol, 'xtest', data)
end

g.test_exception_attempt_to_decode_enum = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}}),
        protobuf.enum('xtest', {ok = 0,
            error1 = 1,})
    })
    local msg = 'Attempt to decode enum "xtest" as a top level message'
    local data = '\b\n'
    t.assert_error_msg_contains(msg, protocol.decode, protocol, 'xtest', data)
end

g.test_exception_reserved_id = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Id 19015 in \"test.<field>\" field ' ..
        'is in reserved id range [19000, 19999]'
    local data = '\xB8\xA4\t\n'
    t.assert_error_msg_contains(msg, protocol.decode, protocol, 'test', data)
end

g.test_exception_not_valid_id = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = 'Id 0 in \"test.<field>\" field is out of range [1; 536870911]'
    local data = '\0\n'
    t.assert_error_msg_contains(msg, protocol.decode, protocol, 'test', data)
end

g.test_exception_incorrect_msb = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local msg = '[test.val] decoding error on position 13: ' ..
        'Incorrect msb for varint to decode'
    local data = '\b\x80\xF0\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x01'
    t.assert_error_msg_contains(msg, protocol.decode, protocol, 'test', data)
end

g.test_exception_interrupted_message = function()
    local testcases = {
        {type = 'int32', data = '\b\xC0\xB5', num = 3},
        {type = 'int64', data = '\b\x80\xEB', num = 3},
        {type = 'float', data = '\rI \xF1', num = 4},
        {type = 'double', data = '\t/\xDD$\x06\x81', num = 6},
        {type = 'fixed32', data = '\r\x01\0\0', num = 4},
        {type = 'sfixed32', data = '\r\x01\0\0', num = 4},
        {type = 'fixed64', data = '\t\x0F\0\0\0\0\0\0', num = 8},
        {type = 'sfixed64', data = '\t\x0F\0\0\0\0\0\0', num = 8},
        {type = 'string', data = '\n\x03fu', num = 4}
    }
    for _, test in pairs(testcases) do
        local protocol = protobuf.protocol({
            protobuf.message('test', {val = {test.type, 1}})
        })
        local msg = ('[test.val] decoding error on position %d: ')
            :format(test.num) .. 'Reached end of message while decoding'
        t.assert_error_msg_contains(msg, protocol.decode, protocol, 'test',
            test.data)
    end
end

-- In this test embedded message is encoded between two varint fields.
-- Last byte in embedded message contains msb for varint field.
-- This byte was intentionally deleted. This leads to an error
-- when decoder runs out of embedded message boundary while trying to
-- decode varint without msb.
g.test_exception_damaged_embedded_message = function()
    local protocol = protobuf.protocol({
        protobuf.message('test_message', {
            val1 = {'int32', 1},
            val2 = {'test_message', 2},
            val3 = {'int32', 20000},
        })
    })
    local data = '\b\f\x12\a\x80\xE2\t\x01\b\xF4\x80\xE2\t\x19'
    local msg = '[test_message.val2.val1] decoding error on position 12: ' ..
        'Reached end of message while decoding'
    t.assert_error_msg_contains(msg, protocol.decode, protocol,
        'test_message', data)
end
