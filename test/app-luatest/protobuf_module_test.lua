local t = require('luatest')
local protobuf = require('protobuf')
local g = t.group()
local utils = require('test/app-luatest/protobuf_utils')

g.test_module_multiple_fields = function()
    local protocol = protobuf.protocol({
        protobuf.message('KeyValue', {
            key = {'bytes', 1},
            index = {'int64', 2},
            number = {'int32', 3},
            version = {'fixed32', 4},
            available = {'bool', 5},
        })
    })
    local result = protocol:encode('KeyValue', {
        key = 'abc',
        number = 25,
        version = 1,
        index = 15,
        available = true,
    })
    local exp = '(\x01\n\x03abc\x10\x0F%\x01\0\0\0\x18\x19'
    utils.strings_deepequal(result, exp)
end

g.test_module_selective_coding = function()
    local protocol = protobuf.protocol({
        protobuf.message('KeyValue', {
            key = {'bytes', 1},
            index = {'int64', 2},
            number = {'int32', 3},
            version = {'fixed32', 4},
            available = {'bool', 5},
        })
    })
    local result = protocol:encode('KeyValue', {
        version = 1,
        key = 'abc',
    })
    local exp = '\n\x03abc%\x01\0\0\0'
    utils.strings_deepequal(result, exp)
end

g.test_module_selective_coding_with_box_NULL = function()
    local protocol = protobuf.protocol({
        protobuf.message('KeyValue', {
            key = {'bytes', 1},
            index = {'int64', 2},
            number = {'int32', 3},
            version = {'fixed32', 4},
            available = {'bool', 5},
        })
    })
    local result = protocol:encode('KeyValue', {
        version = 1,
        key = 'abc',
        number = box.NULL,
    })
    local exp = '\n\x03abc%\x01\0\0\0'
    utils.strings_deepequal(result, exp)
end

g.test_module_multiple_messages = function()
    local protocol = protobuf.protocol({
        protobuf.message('KeyValue', {
            key = {'bytes', 1},
            index = {'int64', 2},
        }),
        protobuf.message('Storage', {
            number = {'int32', 3},
            version = {'fixed32', 4},
            available = {'bool', 5},
        })
    })
    local result = protocol:encode('KeyValue', {
        index = 25,
        key = 'abc',
    })
    local exp = '\n\x03abc\x10\x19'
    utils.strings_deepequal(result, exp)
    local result = protocol:encode('Storage', {
        number = 15,
        available = true,
    })
    exp = '(\x01\x18\x0F'
    utils.strings_deepequal(result, exp)
end

g.test_module_nested_messages = function()
    local protocol = protobuf.protocol({
        protobuf.message('KeyValue', {
            key = {'bytes', 1},
            index = {'int64', 2},
            info = {'Spec', 3},
        }),
        protobuf.message('Spec', {
            number = {'int32', 3},
            version = {'fixed32', 4},
            available = {'bool', 5},
        })
    })
    local result = protocol:encode('KeyValue', {
        index = 25,
        key = 'abc',
        info = {
            number = 15,
            available = true,
            version = 1,
        }
    })
    local exp = '\n\x03abc\x10\x19\x1A\t\x18\x0F(\x01%\x01\0\0\0'
    utils.strings_deepequal(result, exp)
end

g.test_module_message_default_value_encoding = function()
    local protocol = protobuf.protocol({
        protobuf.message('KeyValue', {
            key = {'bytes', 1},
            index = {'int64', 2},
            info = {'Spec', 3},
        }),
        protobuf.message('Spec', {
            number = {'int32', 3},
            version = {'fixed32', 4},
            available = {'bool', 5},
        })
    })
    local result = protocol:encode('KeyValue', {
        index = 25,
        key = 'abc',
        info = {},
    })
    t.assert_str_contains(string.hex(result), '1a00')
    local result = protocol:encode('KeyValue', {
        index = 25,
        key = 'abc',
    })
    t.assert_str_matches(string.hex(result), '0a036162631019')
end

g.test_module_enum_usage = function()
    local protocol = protobuf.protocol({
        protobuf.message('KeyValue', {
            key = {'bytes', 1},
            index = {'int64', 2},
            ret_val = {'ReturnValue', 3},
        }),
        protobuf.enum('ReturnValue', {
            ok = 0,
            error1 = 1,
            error2 = 2,
            error3 = 3,
        })
    })
    local result = protocol:encode('KeyValue', {
        index = 25,
        key = 'abc',
        ret_val = 'error2',
    })
    local exp = '\x10\x19\n\x03abc\x18\x02'
    utils.strings_deepequal(result, exp)
end

g.test_module_enum_default_value_encoding = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            str_ret_val = {'ReturnValue', 1},
            num_ret_val = {'ReturnValue', 2},
        }),
        protobuf.enum('ReturnValue', {
            ok = 0,
            error1 = 1,
        })
    })
    local result = protocol:encode('test', {
        str_ret_val = 'ok',
        num_ret_val = 0,
    })
    t.assert_str_matches(string.hex(result), '')
end

g.test_module_exception_enum_on_top_level = function()
    local protocol = protobuf.protocol({
        protobuf.message('MyMessage', {
            myfield = {'MyEnum', 3},
        }),
        protobuf.enum('MyEnum', {
            ok = 0,
        })
    })
    local msg = 'Attempt to encode enum "MyEnum" as a top level message'
    local data = {myfield = 'ok'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'MyEnum', data)
end

g.test_module_exception_value_in_enum_out_of_int32_range = function()
    local msg = 'Input data for "ok" field is "4294967297" and ' ..
                'do not fit in "int32"'
    local enum_def = {def = 0, ok = 2^32 + 1}
    t.assert_error_msg_contains(msg, protobuf.enum, 'MyEnum', enum_def)
end

g.test_module_exception_enum_value_out_of_int32_range = function()
    local protocol = protobuf.protocol({
        protobuf.message('KeyValue', {
            key = {'bytes', 1},
            index = {'int64', 2},
            ret_val = {'ReturnValue', 3},
        }),
        protobuf.enum('ReturnValue', {
            ok = 0,
            error1 = 1,
            error2 = 2,
            error3 = 3,
        })
    })
    local msg = 'Input data for "nil" field is "4294967297" ' ..
                'and do not fit in "int32"'
    local data = {ret_val = 2^32 + 1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol,
        'KeyValue', data)
end

g.test_module_exception_enum_wrong_value = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            key = {'bytes', 1},
            index = {'int64', 2},
            ret_val = {'ReturnValue', 3},
        }),
        protobuf.enum('ReturnValue', {
            ok = 0,
            error1 = 1,
            error2 = 2,
            error3 = 3,
        })
    })
    local msg = '"error4" is not defined in "ReturnValue" enum'
    local data = {ret_val = 'error4'}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_module_enum_number_encoding = function()
    local protocol = protobuf.protocol({
        protobuf.message('KeyValue', {
            key = {'bytes', 1},
            index = {'int64', 2},
            ret_val = {'ReturnValue', 3},
        }),
        protobuf.enum('ReturnValue', {
            ok = 0,
            error1 = 1,
            error2 = 2,
            error3 = 3,
        })
    })
    local result = protocol:encode('KeyValue', {ret_val = 15})
    t.assert_str_matches(string.hex(result), '180f')
end

g.test_module_exception_empty_protocol = function()
    local protocol = protobuf.protocol({})
    local msg = 'There is no message or enum named "test" ' ..
        'in the given protocol'
    local data = {val = 1.5}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_module_exception_undefined_field = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            val = {'int32', 1}
        }),
    })
    local msg = 'Wrong field name "res" for "test" message'
    local data = {res = 1}
    t.assert_error_msg_contains(msg, protocol.encode, protocol, 'test', data)
end

g.test_module_unknown_fields = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            key = {'bytes', 1},
            index = {'int64', 2},
        })
    })
    local result = protocol:encode('test', {
        index = 10,
        _unknown_fields = {'\x1a\x03\x61\x62\x63'},
    })
    local exp = '\x1A\x03abc\x10\n'
    utils.strings_deepequal(result, exp)
end

g.test_module_unknown_fields_multiple = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            key = {'bytes', 1},
            index = {'int64', 2},
        })
    })
    local result = protocol:encode('test', {
        index = 10,
        _unknown_fields = {'\x1a\x01\x61', '\x22\x01\x62'},
    })
    local exp = '\x10\n\x1A\x01a\"\x01b'
    utils.strings_deepequal(result, exp)
end

g.test_module_exception_name_reusage = function()
    local protocol_def = {
        protobuf.message('test', {
            val = {'int32', 1}
        }),
        protobuf.message('test', {
            res = {'int32', 1}
        })
    }
    local msg = 'Double definition of name "test"'
    t.assert_error_msg_contains(msg, protobuf.protocol, protocol_def)
end

g.test_module_recursive = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            val = {'int32', 1},
            recursion = {'test', 2}
        })
    })
    local result = protocol:encode('test', {
        val = 15,
        recursion = {val = 15},
    })
    t.assert_str_contains(string.hex(result), '1202080f080f')
end

g.test_module_exception_not_declared_type = function()
    local protocol_def = {
        protobuf.message('test', {
            val = {'int32', 1},
            recursion = {'value', 2}
        })
    }
    local msg = 'Type "value" is not declared'
    t.assert_error_msg_contains(msg, protobuf.protocol, protocol_def)
end

g.test_module_exception_repeated_id = function()
    local message_name = 'test'
    local message_def = {
        val1 = {'int32', 1},
        val2 = {'int32', 1}
    }
    local msg = 'Id 1 in field "val1" was already used'
    t.assert_error_msg_contains(msg, protobuf.message,
        message_name, message_def)
end

g.test_module_exception_id_out_of_range = function()
    local message_name = 'test'
    local message_def = {
        val = {'int32', 2^32}
    }
    local msg = 'Id 4294967296 in "val" field is out of range [1; 536870911]'
    t.assert_error_msg_contains(msg, protobuf.message,
        message_name, message_def)
end

g.test_module_exception_id_in_prohibited_range = function()
    local message_name = 'test'
    local message_def = {
        val = {'int32', 19000}
    }
    local msg = 'Id 19000 in "val" field is in reserved ' ..
        'id range [19000, 19999]'
    t.assert_error_msg_contains(msg, protobuf.message,
        message_name, message_def)
end

g.test_module_exception_enum_double_definition = function()
    local enum_name = 'test'
    local enum_def = {
        ok = 0,
        error1 = 2,
        error2 = 2,
        error3 = 3,
    }
    local msg = 'Double definition of enum field "error2" by 2'
    t.assert_error_msg_contains(msg, protobuf.enum, enum_name, enum_def)
end

g.test_module_exception_enum_missing_zero = function()
    local enum_name = 'test'
    local enum_def = {
        error1 = 1,
        error2 = 2,
        error3 = 3,
    }
    local msg = '"test" definition does not contain a field with id = 0'
    t.assert_error_msg_contains(msg, protobuf.enum, enum_name, enum_def)
end

-- Previous encoding implementation had a bug of signed integer overflow
-- which led to different behaviour of interpreted and JITted code.
-- This test repeates encoding process enough times to JIT the code and
-- checks result at each iteration.
g.test_repetitive_int64_encoding = function()
   local protocol = protobuf.protocol({
        protobuf.message('test', {
            index = {'int64', 1},
        })
    })
    for _ = 1, 300 do
        local result = protocol:encode('test', {
            index = -770,
        })
        t.assert_str_contains(string.hex(result), '08fef9ffffffffffffff01')
    end
end

g.test_marshalling_message_with_scalar_fields = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            message = {'string', 1},
            status = {'int32', 2},
            ok = {'bool', 3},
        })
    })
    local primary_result = protocol:encode('test',
        {message = 'this is message', status = 200, ok = true})
    local decoded_data = protocol:decode('test', primary_result)
    local secondary_result = protocol:encode('test', decoded_data)
    return utils.strings_deepequal(primary_result, secondary_result)
end

g.test_marshalling_message_with_scalar_fields_and_enum = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            message = {'string', 1},
            status = {'status', 2},
            ok = {'bool', 3},
        }),
        protobuf.enum('status', {
            ok = 0,
            err1 = 1,
            err2 = 2,
            err3 = 3,
        })
    })
    local primary_result = protocol:encode('test',
        {message = 'this is message', status = 'err1', ok = true})
    local decoded_data = protocol:decode('test', primary_result)
    local secondary_result = protocol:encode('test', decoded_data)
    return utils.strings_deepequal(primary_result, secondary_result)
end
