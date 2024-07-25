local t = require('luatest')
local protobuf = require('protobuf')
local fio = require('fio')
local g = t.group()
local work_dir = fio.tempdir()
local file_path = fio.pathjoin(work_dir, "test_file.proto")


local p = t.group('single_message', t.helpers.matrix({
    prefix = {'', 'repeated '},
    type = {'int32', 'sint32', 'uint32', 'int64', 'sint64', 'uint64',
        'fixed32', 'sfixed32', 'fixed64', 'sfixed64', 'bool', 'float',
        'double'},
}))

p.test_single_message = function(cg)
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'message test {\n' ..
        '    ' .. cg.params.prefix .. cg.params.type .. ' val1 = 1;\n' ..
        '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            val1 = {cg.params.prefix .. cg.params.type, 1},
        })
    })
    local result = protobuf.parse(file_path)
    t.assert_equals(result, protocol)
end

g.test_enum = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'enum test {\n' ..
        '    ok = 0;\n' ..
        '    error1 = 1;\n' ..
        '    error2 = 2;\n' ..
        '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.enum('test', {
            ok = 0,
            error1 = 1,
            error2 = 2,
        })
    })
    local result = protobuf.parse(file_path)
    t.assert_equals(result, protocol)
end

g.test_nested_messages = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'message message1 {\n' ..
        '    bytes val1 = 1;\n' ..
        '    message2 val2 = 2;\n' ..
        '}' ..
        'message message2 {\n' ..
        '    int32 val1 = 1;\n' ..
        '    message1 val2 = 2;\n' ..
        '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('message1', {
            val1 = {'bytes', 1},
            val2 = {'message2', 2},
        }),
        protobuf.message('message2', {
            val1 = {'int32', 1},
            val2 = {'message1', 2},
        })
    })
    local result = protobuf.parse(file_path)
    t.assert_equals(result, protocol)
end

g.test_message_internal_definition = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = "proto3";\n' ..
        'message message1 {\n' ..
        '    bytes val1 = 1;\n' ..
        '    message message2 {\n' ..
        '        int32 val1 = 1;\n' ..
        '        string val2 = 2;\n' ..
        '    }\n' ..
        '    int32 val2 = 2;\n' ..
        '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('message1', {
            val1 = {'bytes', 1},
            val2 = {'int32', 2},
        }),
        protobuf.message('message2', {
            val1 = {'int32', 1},
            val2 = {'string', 2},
        })
    })
    local result = protobuf.parse(file_path)
    t.assert_equals(result, protocol)
end

g.test_enum_internal_definition = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = "proto3";\n' ..
        'message test_message {\n' ..
        '    bytes val1 = 1;\n' ..
        '    enum test_enum {\n' ..
        '        default = 0;\n' ..
        '        value = 1;\n' ..
        '    }\n' ..
        '    int32 val2 = 2;\n' ..
        '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('test_message', {
            val1 = {'bytes', 1},
            val2 = {'int32', 2},
        }),
        protobuf.enum('test_enum', {
            default = 0;
            value = 1;
        })
    })
    local result = protobuf.parse(file_path)
    t.assert_equals(result, protocol)
end

local p = t.group('exception_incorrect_syntax_definition', {
    {syntax = 'syntaxx = "proto3";\n',
        msg = 'Syntax level must be defined as first declaration in file'},
    {syntax = 'syntax "proto3";\n',
        msg = 'Missing = after "syntax"'},
    {syntax = 'syntax = 12;\n',
        msg = 'Syntax level must be defined by string value'},
    {syntax = 'syntax = "proto2";\n',
        msg = 'Only proto3 syntax level is supported'},
    {syntax = 'syntax = "proto3"\n',
        msg = 'Expected ; at the end of the line'},
})

p.test_exception_incorrect_syntax_definition = function(cg)
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        cg.params.syntax ..
        'message test {\n' ..
        '    int32 val1 = 1;\n' ..
        '}'
    )
    fh:close()
    t.assert_error_msg_contains(cg.params.msg, protobuf.parse, file_path)
end

local p = t.group('exception_incorrect_message_field_definition', {
    {field_def = 'int32 val1 = 1\n',
        msg = 'Expected ; at the end of the line'},
    {field_def = 'int32 val1 1;\n',
        msg = 'Expected = after the field: val1'},
    {field_def = 'int33 val1 = 1;\n',
        msg = 'int33 declaration is missing in protocol'},
    {field_def = '1int32 val1 = 1;\n',
        msg = 'A number: 1, contains letter: i'},
    {field_def = 'int32 12 = 1;\n',
        msg = 'Field name must be identificator, got: 12'},
    {field_def = 'string string = 1;\n',
        msg = 'Field name must be identificator, got: string'},
    {field_def = 'optional string val1 = 1;\n',
        msg = 'Unsupported keyword: optional'},
    {field_def = 'string map = 1;\n',
        msg = 'Unsupported keyword: map'},
    {field_def = 'int32 val1 = 1.5;\n',
        msg = 'Field id must be integer, got: 1.5'},
    {field_def = 'int32 val1 = 0;\n',
        msg = 'Id 0 in field "val1" is out of range [1; 536870911]'},
    {field_def = 'int32 val1 = 19001;\n', msg = 'Id 19001 in field "val1" ' ..
        'is in reserved id range [19000, 19999]'},
    {field_def = 'int32 val1 = 536870912;\n',
        msg = 'Id 536870912 in field "val1" is out of range [1; 536870911]'},
    {field_def = 'int32 val1 = 091;\n',
        msg = 'Incorrect digit: 9 for octal value: 0'},
    {field_def = 'int32 val1 = 0xf9ao;\n',
        msg = 'Incorrect digit: o for hex value: f9a'},
    {field_def = 'int32 val1 = 1;\n     int32 val2 = 1;\n',
        msg = 'Field id 1 isn`t unique in "test" message'},
    {field_def = 'int32 val1 = 1;\n     int32 val1 = 2;\n',
        msg = 'Field name "val1" isn`t unique in "test" message'},
})

p.test_exception_incorrect_message_field_definition = function(cg)
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'message test {\n' ..
        '    ' .. cg.params.field_def ..
        '}'
    )
    fh:close()
    t.assert_error_msg_contains(cg.params.msg, protobuf.parse, file_path)
end

local p = t.group('exception_incorrect_enum_field_definition', {
    {enum_def = 'default = 1;\n    value = 2;\n',
        msg = 'Enum test does not contain field with id 0'},
    {enum_def = 'default = 0;\n    string = 2;\n',
        msg = 'Field name must be identificator, got: string'},
    {enum_def = 'default 0;\n    value = 1;\n',
        msg = 'Expected = after the field: default'},
    {enum_def = 'default = 0;\n    value = 1.5;\n',
        msg = 'Enum value must be integer, got: 1.5'},
    {enum_def = 'default = 0;\n    default = 0;\n',
        msg = 'Value "default" isn`t unique in "test" enum'},
    {enum_def = 'default = 0;\n    value = 0;\n',
        msg = 'Value id 0 isn`t unique in "test" enum'},
    {enum_def = 'default = 0\n    value = 1;\n',
        msg = 'Expected ; at the end of the line'},
    {enum_def = 'default = 0;\n    value = 2147483648;\n',
        msg = 'Id 2147483648 in field "value" is out of ' ..
            'range [-2147483648; 2147483647]'},
    {enum_def = 'default = 0;\n    value = -2147483649;\n',
        msg = 'Id -2147483649 in field "value" is out of ' ..
            'range [-2147483648; 2147483647]'},
})

p.test_exception_incorrect_enum_field_definition = function(cg)
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'enum test {\n' ..
        '    ' .. cg.params.enum_def ..
        '}'
    )
    fh:close()
    t.assert_error_msg_contains(cg.params.msg, protobuf.parse, file_path)
end
