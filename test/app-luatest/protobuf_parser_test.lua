local t = require('luatest')
local fio = require('fio')
local protobuf = require('protobuf')
local parser = require('internal.protobuf.parser')
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
        'package package;\n' ..
        'message test {\n' ..
        '    ' .. cg.params.prefix .. cg.params.type .. ' val1 = 1;\n' ..
        '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('package.test', {
            ['package.test.val1'] = {cg.params.prefix .. cg.params.type, 1},
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
    t.assert_equals(result, protocol)
end

local p = t.group('single_oneof', t.helpers.matrix({
    type = {'int32', 'sint32', 'uint32', 'int64', 'sint64', 'uint64',
        'fixed32', 'sfixed32', 'fixed64', 'sfixed64', 'bool', 'float',
        'double'},
}))

p.test_single_oneof = function(cg)
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'package package;\n' ..
        'message test {\n' ..
        '    oneof test_oneof {\n' ..
        '        ' .. cg.params.type .. ' val1 = 1;\n' ..
        '        ' .. cg.params.type .. ' val2 = 2;\n' ..
        '    }\n' ..
        '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('package.test', {
            ['package.test.val1'] = {cg.params.type, 1},
            ['package.test.val2'] = {cg.params.type, 2},
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
    t.assert_equals(result, protocol)
end

local p = t.group('single_map', t.helpers.matrix({
    key_type = {'int32', 'sint32', 'uint32', 'int64', 'sint64', 'uint64',
        'fixed32', 'sfixed32', 'fixed64', 'sfixed64', 'bool', 'string'},
    value_type = {'int32', 'sint32', 'uint32', 'int64', 'sint64', 'uint64',
        'fixed32', 'sfixed32', 'fixed64', 'sfixed64', 'bool', 'float',
        'double', 'test_message', 'test_enum'},
}))

p.test_single_map = function(cg)
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'enum test_enum {\n' ..
        '    ok = 0;\n' ..
        '    error1 = 1;\n' ..
        '}' ..
        'message test_message {\n' ..
        '    map<' .. cg.params.key_type .. ',' .. cg.params.value_type ..
            '> test_map = 1;\n' ..
        '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.enum('test_enum', {
            ok = 0,
            error1 = 1,
        }),
        protobuf.message('test_message.TestMapEntry', {
            ['key'] = {cg.params.key_type, 1},
            ['value'] = {cg.params.value_type, 2},
        }),
        protobuf.message('test_message', {
            ['test_message.test_map'] =
                {'repeated test_message.TestMapEntry' , 1},
        })
    })
    protocol['test_message.TestMapEntry']['map_entry'] = true
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
    t.assert_equals(result, protocol)
end

g.test_package_declaration_at_the_end_of_the_file = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'message test {\n' ..
        '    int32 val1 = 1;\n' ..
        '}'  ..
        'package foo.bar;\n'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('foo.bar.test', {
            ['foo.bar.test.val1'] = {'int32', 1},
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
    t.assert_equals(result, protocol)
end

local p = t.group('external_file_import', {
    {import_name = '\'external\\b.proto\';\n',
        result = {['public'] = 'external\\b.proto'}},
    {import_name = 'public \'b.proto\';\n',
        result = {['public'] = 'b.proto'}},
    {import_name = 'weak \'b.proto\';\n',
        result = {['weak'] = 'b.proto'}},
})

p.test_external_file_import = function(cg)
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'import ' .. cg.params.import_name ..
        'message test {\n' ..
        '    int32 val1 = 1;\n' ..
        '}'  ..
        'package foo.bar;\n'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('foo.bar.test', {
            ['foo.bar.test.val1'] = {'int32', 1},
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, import = parser.parser(fh)
    t.assert_equals(result, protocol)
    t.assert_equals(import, cg.params.result)
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
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
    t.assert_equals(result, protocol)
end

g.test_keyword_message_name = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
       'syntax = \'proto3\';\n' ..
       'message message {\n' ..
       '    int32 val1 = 1;\n' ..
       '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('message', {
            ['message.val1'] = {'int32', 1},
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
    t.assert_equals(result, protocol)
end

g.test_keyword_enum_name = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
       'syntax = \'proto3\';\n' ..
       'enum message {\n' ..
       '    val1 = 0;\n' ..
       '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.enum('message', {
            val1 = 0,
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
    t.assert_equals(result, protocol)
end

g.test_unsupproted_keyword_message_name = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
       'syntax = \'proto3\';\n' ..
       'message map {\n' ..
       '    int32 val1 = 1;\n' ..
       '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('map', {
            ['map.val1'] = {'int32', 1},
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
    t.assert_equals(result, protocol)
end

g.test_unsupported_keyword_enum_name = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
       'syntax = \'proto3\';\n' ..
       'enum map {\n' ..
       '    val1 = 0;\n' ..
       '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.enum('map', {
            val1 = 0,
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
    t.assert_equals(result, protocol)
end

g.test_skipped_declarative_keywords = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'edition = \'2023\';\n' ..
        'option = \'add_feature\';\n' ..
        'message test {\n' ..
        '    optional int32 val1 = 1;\n' ..
        '    [json_name = "Enroll status"]\n' ..
        '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('test', {
            ['test.val1'] = {'int32', 1},
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
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
            ['message1.val1'] = {'bytes', 1},
            ['message1.val2'] = {'message2', 2},
        }),
        protobuf.message('message2', {
            ['message2.val1'] = {'int32', 1},
            ['message2.val2'] = {'message1', 2},
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
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
            ['message1.val1'] = {'bytes', 1},
            ['message1.val2'] = {'int32', 2},
        }),
        protobuf.message('message1.message2', {
            ['message1.message2.val1'] = {'int32', 1},
            ['message1.message2.val2'] = {'string', 2},
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
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
            ['test_message.val1'] = {'bytes', 1},
            ['test_message.val2'] = {'int32', 2},
        }),
        protobuf.enum('test_message.test_enum', {
            default = 0;
            value = 1;
        })
    })
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, _, _ = parser.parser(fh)
    t.assert_equals(result, protocol)
end

g.test_undeclared_field_type = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = "proto3";\n' ..
        'message message1 {\n' ..
        '    bytes val1 = 1;\n' ..
        '    undeclared_type val2 = 2;\n' ..
        '}'
    )
    fh:close()
    local protocol = protobuf.protocol({
        protobuf.message('message1', {
            ['message1.val1'] = {'bytes', 1},
        }),
    })
    protocol['message1']['field_by_id'][2] =
        {id = 2, name = "message1.val2", type = "undeclared_type"}
    protocol['message1']['field_by_name']['message1.val2'] =
        {id = 2, name = "message1.val2", type = "undeclared_type"}
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local result, undeclared = parser.parser(fh)
    t.assert_equals(result, protocol)
    t.assert_equals(undeclared, {['undeclared_type'] = true})
end

local p = t.group('exception_incorrect_syntax_definition', {
    {syntax = 'package package;\n',
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
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    t.assert_error_msg_contains(cg.params.msg, parser.parser, fh)
end

g.test_exception_package_declared_twice = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'package package;\n' ..
        'message test {\n' ..
        '    int32 val1 = 1;\n' ..
        '}'  ..
        'package package;\n'
    )
    fh:close()
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local msg = 'Package can be declared only once'
    t.assert_error_msg_contains(msg, parser.parser, fh)
end

local p = t.group('exception_incorrect_package_definition', {
    {package = 'package 12;\n',
        msg = 'Package name must be identificator, got: integer'},
    {package = 'package package"\n',
        msg = 'Expected ; at the end of the line'},
})

p.test_exception_incorrect_package_definition = function(cg)
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n'..
        cg.params.package ..
        'message test {\n' ..
        '    int32 val1 = 1;\n' ..
        '}'
    )
    fh:close()
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    t.assert_error_msg_contains(cg.params.msg, parser.parser, fh)
end

local p = t.group('exception_incorrect_import_definition', {
    {import_name = 'b.proto;\n',
        msg = 'Import name must be defined by string value'},
    {import_name = 'weeeak \'b.proto\';\n',
        msg = 'Import name must be defined by string value'},
    {import_name = 'public \'b.proto\'\n',
        msg = 'Expected ; at the end of the line'},
})

p.test_exception_incorrect_import_definition = function(cg)
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n'..
        'import ' .. cg.params.import_name ..
        'message test {\n' ..
        '    int32 val1 = 1;\n' ..
        '}'
    )
    fh:close()
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    t.assert_error_msg_contains(cg.params.msg, parser.parser, fh)
end

local p = t.group('exception_incorrect_message_field_definition', {
    {field_def = 'int32 val1 = 1\n',
        msg = 'Expected ; at the end of the line'},
    {field_def = 'int32 val1 1;\n',
        msg = 'Expected = after the field: test.val1'},
    {field_def = '1int32 val1 = 1;\n',
        msg = 'A number: 1, contains letter: i'},
    {field_def = 'int32 12 = 1;\n',
        msg = 'Field name must be identificator, got: integer'},
    {field_def = 'int32 val1 = 1.5;\n',
        msg = 'Field id must be integer, got: 1.5'},
    {field_def = 'int32 val1 = 0;\n',
        msg = 'Id 0 in field "test.val1" is out of range [1; 536870911]'},
    {field_def = 'int32 val1 = 19001;\n',
        msg = 'Id 19001 in field "test.val1" is in reserved id ' ..
            'range [19000, 19999]'},
    {field_def = 'int32 val1 = 536870912;\n',
        msg = 'Id 536870912 in field "test.val1" is out of ' ..
            'range [1; 536870911]'},
    {field_def = 'int32 val1 = 091;\n',
        msg = 'Incorrect digit: 9 for octal value: 0'},
    {field_def = 'int32 val1 = 0xf9ao;\n',
        msg = 'Incorrect digit: o for hex value: f9a'},
    {field_def = 'int32 val1 = 1;\n     int32 val2 = 1;\n',
        msg = 'Field id 1 isn`t unique in "test" message'},
    {field_def = 'int32 val1 = 1;\n     int32 val1 = 2;\n',
        msg = 'Field name "test.val1" isn`t unique in "test" message'},
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
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    t.assert_error_msg_contains(cg.params.msg, parser.parser, fh)
end

local p = t.group('exception_incorrect_enum_field_definition', {
    {enum_def = 'default = 1;\n    value = 2;\n',
        msg = 'Enum test does not contain field with id 0'},
    {enum_def = 'default 0;\n    value = 1;\n',
        msg = 'Expected = after the field: default'},
    {enum_def = 'default = 0;\n    value = 1.5;\n',
        msg = 'Enum value must be integer, got: 1.5'},
    {enum_def = 'default = 0;\n    default = 1;\n',
        msg = 'Value "default" isn`t unique in "test" enum'},
    {enum_def = '0 = 0;\n    default = 0;\n',
        msg = 'Field name must be identificator, got: integer'},
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
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    t.assert_error_msg_contains(cg.params.msg, parser.parser, fh)
end

g.test_exception_incorrect_message_name = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
       'syntax = \'proto3\';\n' ..
       'message 12 {\n' ..
       '    int32 val1 = 1;\n' ..
       '}'
    )
    fh:close()
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local msg = 'Message name must be identificator, got: integer'
    t.assert_error_msg_contains(msg, parser.parser, fh)
end

g.test_exception_incorrect_enum_name = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
       'syntax = \'proto3\';\n' ..
       'enum 12 {\n' ..
       '    val1 = 0;\n' ..
       '}'
    )
    fh:close()
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local msg = 'Enum name must be identificator, got: integer'
    t.assert_error_msg_contains(msg, parser.parser, fh)
end

g.test_exception_oneof_contains_repeated_field = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
       'syntax = \'proto3\';\n' ..
       'message test {\n' ..
       '    oneof test_oneof {\n' ..
       '        repeated int32 val1 = 1;\n' ..
       '        string val2 = 2;\n' ..
       '    }\n' ..
       '}'
    )
    fh:close()
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local msg = 'test oneof contains repeated field'
    t.assert_error_msg_contains(msg, parser.parser, fh)
end

local p = t.group('exception_incorrect_map_definition', {
    {map_def = 'map int32, int32> test_map = 1;\n',
        msg = 'Expected "<" before map types declaration'},
    {map_def = 'map <test_message, int32> test_map = 1;\n',
        msg = 'Key type for map can`t be non-scalar type, got: test_message'},
    {map_def = 'map <float, int32> test_map = 1;\n',
        msg = 'Key type has to be from allowed scalar key_types, got: float'},
    {map_def = 'map <int32 int32> test_map = 1;\n',
        msg = 'Expected "," between types in map daclaration'},
    {map_def = 'map <int32, message> test_map = 1;\n',
        msg = 'Incorrect type for field: message'},
    {map_def = 'map <int32, int32 test_map = 1;\n',
        msg = 'Expected ">" after map types declaration'},
    {map_def = 'map <int32, int32> 12 = 1;\n',
        msg = 'Map name must be identificator, got: integer'},
    {map_def = 'map <int32, int32> test_map 1;\n',
        msg = 'Expected "=" after map name'},
    {map_def = 'map <int32, int32> test_map = number;\n',
        msg = 'Field id must be integer, got: number'},
    {map_def = 'map <int32, int32> test_map = 0;\n',
        msg = 'Id 0 in field "test_map" is out of range [1; 536870911]'},
    {map_def = 'map <int32, int32> test_map = 19001;\n',
        msg = 'Id 19001 in field "test_map" is in reserved ' ..
            'id range [19000, 19999]'},
    {map_def = 'map <int32, int32> test_map = 536870912;\n',
        msg = 'Id 536870912 in field "test_map" is out of ' ..
            'range [1; 536870911]'},
    {map_def = 'map <int32, int32> test_map = 1\n',
        msg = 'Expected ; at the end of the line'},
})

p.test_exception_incorrect_map_definition = function(cg)
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'message test_message {\n' ..
        '    ' .. cg.params.map_def ..
        '}'
    )
    fh:close()
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    t.assert_error_msg_contains(cg.params.msg, parser.parser, fh)
end

g.test_exception_map_name_not_unique = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'message test_message {\n' ..
        '    int32 test_map = 1;\n' ..
        '    map <int32, int32> test_map = 2;\n' ..
        '}'
    )
    fh:close()
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local msg = 'Field name "test_message.test_map" isn`t unique ' ..
        'in "test_message" message'
    t.assert_error_msg_contains(msg, parser.parser, fh)
end

g.test_exception_map_id_not_unique = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'message test_message {\n' ..
        '    int32 val1 = 1;\n' ..
        '    map <int32, int32> test_map = 1;\n' ..
        '}'
    )
    fh:close()
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local msg = 'Field id 1 isn`t unique in "test_message" message'
    t.assert_error_msg_contains(msg, parser.parser, fh)
end

g.test_exception_message_field_appeals_to_internal_message = function()
    local fh = fio.open(file_path, {'O_RDWR', 'O_CREAT', 'O_TRUNC'})
    fh:write(
        'syntax = \'proto3\';\n' ..
        'message message1 {\n' ..
        '    map <int32, int32> test_map = 1;\n' ..
        '}' ..
        'message message2 {\n' ..
        '    message1.TestMapEntry val1 = 1;\n' ..
        '}'
    )
    fh:close()
    fh = fio.open(file_path, 'O_RDONLY')
    assert(fh ~= nil)
    local msg = 'The synthetic map entry message message1.TestMapEntry ' ..
        'may not be directly referenced by other field definitions'
    t.assert_error_msg_contains(msg, parser.parser, fh)
end
