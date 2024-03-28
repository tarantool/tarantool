local t = require('luatest')
local protobuf = require('protobuf')
local g = t.group()

g.test_module_simple = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protobuf.encode(protocol, 'test', {val = 0})
    t.assert_equals(string.hex(result), '0800')
end

g.test_module_encode_method = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'int32', 1}})
    })
    local result = protocol:encode('test', {val = 0})
    t.assert_equals(string.hex(result), '0800')
end

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
    t.assert_str_contains(string.hex(result), '2801')
    t.assert_str_contains(string.hex(result), '0a03616263')
    t.assert_str_contains(string.hex(result), '100f')
    t.assert_str_contains(string.hex(result), '2501000000')
    t.assert_str_contains(string.hex(result), '1819')
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
    t.assert_str_contains(string.hex(result), '0a03616263')
    t.assert_str_contains(string.hex(result), '2501000000')
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
    t.assert_str_contains(string.hex(result), '0a03616263')
    t.assert_str_contains(string.hex(result), '1019')
    local result = protocol:encode('Storage', {
        number = 15,
        available = true,
    })
    t.assert_str_contains(string.hex(result), '2801')
    t.assert_str_contains(string.hex(result), '180f')
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
    t.assert_str_contains(string.hex(result), '1019')
    t.assert_str_contains(string.hex(result), '0a03616263')
    t.assert_str_contains(string.hex(result), '1a09')
    t.assert_str_contains(string.hex(result), '180f')
    t.assert_str_contains(string.hex(result), '2501000000')
    t.assert_str_contains(string.hex(result), '2801')
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
    t.assert_str_contains(string.hex(result), '1019')
    t.assert_str_contains(string.hex(result), '0a03616263')
    t.assert_str_contains(string.hex(result), '1802')
end

g.test_module_enum_encoding = function()
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
    local result = protocol:encode('ReturnValue', {})
    t.assert_str_matches(result, '')
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
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
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
    local msg = 'Wrong message name "test" for this protocol'
    local data = {val = 1.5}
    t.assert_error_msg_contains(msg, protobuf.encode, protocol, 'test', data)
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
        val1 = 1.5,
        val2 = 'some data',
    })
    t.assert_str_contains(string.hex(result), '100a')
    t.assert_str_contains(string.hex(result), '0203312e35')
    t.assert_str_contains(string.hex(result), '0a09736f6d652064617461')
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

g.test_module_exception_recursive = function()
    local protocol_def = {
        protobuf.message('test', {
            val = {'int32', 1},
            recursion = {'test', 2}
        })
    }
    local msg = 'Message "test" has a field of "test" type ' ..
        'Recursive definition is not allowed'
    t.assert_error_msg_contains(msg, protobuf.protocol, protocol_def)
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
    local msg = 'Id 4294967296 in field "val" is out of range [1; 536870911]'
    t.assert_error_msg_contains(msg, protobuf.message,
        message_name, message_def)
end

g.test_module_exception_id_in_prohibited_range = function()
    local message_name = 'test'
    local message_def = {
        val = {'int32', 19000}
    }
    local msg = 'Id 19000 in field "val" is in reserved ' ..
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
