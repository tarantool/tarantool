-- These constants are used to define the boundaries of valid field ids.
-- Described in more detail here:
-- https://protobuf.dev/programming-guides/proto3/#assigning
local MIN_FIELD_ID = 1
local RESERVED_FIELD_ID_MIN = 19000
local RESERVED_FIELD_ID_MAX = 19999
local MAX_FIELD_ID = 2^29 - 1

local MIN_ENUM_ID = -2^31
local MAX_ENUM_ID = 2^31 - 1

local LEX_SYNTAX = 1
local LEX_MESSAGE = 2
local LEX_ENUM = 3
local LEX_IMPORT = 4
local LEX_WEAK = 5
local LEX_PUBLIC = 6
local LEX_REPEATED = 5
local LEX_BYTES = 6
local LEX_INT32 = 7
local LEX_INT64 = 8
local LEX_UINT32 = 9
local LEX_UINT64 = 10
local LEX_SINT32 = 11
local LEX_SINT64 = 12
local LEX_FIXED32 = 13
local LEX_FIXED64 = 14
local LEX_SFIXED32 = 15
local LEX_SFIXED64 = 16
local LEX_BOOL = 17
local LEX_STRING = 18
local LEX_FLOAT = 19
local LEX_DOUBLE = 20
local LEX_PACKAGE = 21
local LEX_ONEOF = 22
local LEX_MAP = 23

local keywords = {
    supported = {
        syntax = LEX_SYNTAX,
        message = LEX_MESSAGE,
        enum = LEX_ENUM,
        package = LEX_PACKAGE,
        repeated = LEX_REPEATED,
        oneof = LEX_ONEOF,
        map = LEX_MAP,
        string = LEX_STRING,
        bytes = LEX_BYTES,
        int32 = LEX_INT32,
        int64 = LEX_INT64,
        uint32 = LEX_UINT32,
        uint64 = LEX_UINT64,
        sint32 = LEX_SINT32,
        sint64 = LEX_SINT64,
        fixed32 = LEX_FIXED32,
        fixed64 = LEX_FIXED64,
        sfixed32 = LEX_SFIXED32,
        sfixed64 = LEX_SFIXED64,
        bool = LEX_BOOL,
        float = LEX_FLOAT,
        double = LEX_DOUBLE,
        import = LEX_IMPORT,
        weak = LEX_WEAK,
        public = LEX_PUBLIC,
    },
    ignored = {
        edition = 'edition',
        option = 'option',
    },
    unsupported = {
        inf = 'inf',
        nan = 'nan',
        service = 'service',
        extend = 'extend',
        group = 'group',
        extensions = 'extensions',
        reserved = 'reserved',
        rpc = 'rpc',
        stream = 'stream',
        returns = 'returns',
        to = 'to',
        max = 'max',
        optional = 'optional',
        required = 'required',
    },
}

-- Forward declarations
local parse_message
local parse_enum
local parse_map

-- {{{ Helpers

local function isspace(byte)
    return string.match(byte, '%s')
end

local function isalpha(byte)
    return string.match(byte, '%a')
end

local function isdigit(byte)
    return string.match(byte, '%d')
end

local function isoctal(byte)
    return byte >= '0' and byte < '8'
end

local function ishex(byte)
    return string.match(byte, '%x')
end

local function feof(ctx)
    return ctx.pos > string.len(ctx.buffer)
end

local function get_byte(ctx)
    local byte = string.char(string.byte(ctx.buffer, ctx.pos))
    ctx.pos = ctx.pos + 1
    return byte, ctx
end

local function validate_message_id(protocol, message_name, ctx, field_name)
    if ctx.lex.type ~= 'integer' then
        error(('Field id must be integer, got: %s'):format(ctx.lex.val))
    end
    if ctx.lex.val < MIN_FIELD_ID or ctx.lex.val > MAX_FIELD_ID then
        error(('Id %d in field %q is out of range [%d; %d]'):format(
             ctx.lex.val, field_name, MIN_FIELD_ID, MAX_FIELD_ID))
    end
    if ctx.lex.val >= RESERVED_FIELD_ID_MIN and
        ctx.lex.val <= RESERVED_FIELD_ID_MAX then
        error(('Id %d in field %q is in reserved ' ..
            'id range [%d, %d]'):format(ctx.lex.val, field_name,
            RESERVED_FIELD_ID_MIN, RESERVED_FIELD_ID_MAX))
    end
    if protocol[message_name]['field_by_id'][ctx.lex.val] ~= nil then
        error(('Field id %d isn`t unique in "%s" message'):format(
            ctx.lex.val, message_name))
    end
end

local function validate_enum_id(protocol, enum_name, ctx, value)
    if ctx.lex.type ~= 'integer' then
        error(('Enum value must be integer, got: %s'):format(ctx.lex.val))
    end
    if ctx.lex.val < MIN_ENUM_ID or ctx.lex.val > MAX_ENUM_ID then
        error(('Id %d in field %q is out of range [%d; %d]'):format(
             ctx.lex.val, value, MIN_ENUM_ID, MAX_ENUM_ID))
    end
    if protocol[enum_name]['value_by_id'][ctx.lex.val] ~= nil then
        error(('Value id %d isn`t unique in "%s" enum'):format(
            ctx.lex.val, enum_name))
    end
end

local function is_keyword(lex_type)
    return type(lex_type) == 'number' or keywords.unsupported[lex_type] ~= nil
       or lex_type == 'ignored'
end

local function PascalCase(snake_case)
    local res = ''
    for word in string.gmatch(snake_case, "([^_]+)") do
        res = res .. string.upper(string.sub(word, 1, 1)) ..
            string.sub(word, 2)
    end
    return res
end

local function validate_name(ctx, object_name, protocol)
    if ctx.lex.type ~= 'ident' and not is_keyword(ctx.lex.type) then
        error(('%s name must be identificator, got: %s'):format(
            object_name, ctx.lex.type))
    end
    if protocol ~= nil and protocol[ctx.lex.val] ~= nil then
        error(('Object with same name: %s has been already declared'):format(
            ctx.lex.val))
    end
end

local function validate_value_name(protocol, enum_name, ctx)
    if ctx.lex.type ~= 'ident' and not is_keyword(ctx.lex.type) then
        error(('Field name must be identificator, got: %s'):format(
            ctx.lex.type))
    end
    assert(protocol ~= nil)
    assert(protocol[enum_name] ~= nil)
    if protocol[enum_name]['id_by_value'][ctx.lex.val] ~= nil then
        error(('Value "%s" isn`t unique in "%s" enum'):format(
            ctx.lex.val, enum_name))
    end
end

local function validate_field_name(protocol, message_name, ctx, object_name)
    if ctx.lex.type ~= 'ident' and not is_keyword(ctx.lex.type) then
        error(('%s name must be identificator, got: %s'):format(
            object_name, ctx.lex.type))
    end
    local field_name = message_name .. '.' .. ctx.lex.val
    assert(protocol ~= nil)
    assert(protocol[message_name] ~= nil)
    if protocol[message_name]['field_by_name'][field_name] ~= nil then
        error(('Field name "%s" isn`t unique in "%s" message'):format(
            field_name, message_name))
    end
end

local function validate_field_type(ctx, protocol, forward_dec)
    if keywords.supported[ctx.lex.val] ~= nil and
        ctx.lex.type <= LEX_REPEATED then
            error(('Incorrect type for field: %s'):format(ctx.lex.val))
    elseif keywords.unsupported[ctx.lex.val] ~= nil then
        error(('Unsupported keyword: %s can`t be a field type'):format(
            ctx.lex.val))
    elseif keywords.supported[ctx.lex.val] == nil and
        protocol[ctx.lex.val] == nil then
            forward_dec[ctx.lex.val] = true
    end
end

local function check_punctuation(ctx, exp_value, error_msg)
    if ctx.lex.val ~= exp_value then
        error(error_msg)
    end
end

-- }}} Helpers

-- {{{ Lexer

local case = {
    START = function(ctx, byte)
        ctx.lex.val = nil
        if not isspace(byte) then
            ctx.lex.type = nil
            if isalpha(byte) or byte == '_' then
                ctx.lex.val = byte
                ctx.lex.type = 'ident'
                return ctx, 'IDENT'
            elseif isdigit(byte) or byte == '-' then
                ctx.lex.val = byte
                ctx.lex.type = 'integer'
                return ctx, 'INTG'
            elseif byte == '.' then
                ctx.lex.val = '0.'
                ctx.lex.type = 'float'
                return ctx, 'FLOAT'
            elseif byte == '"' or byte == "'" then
                ctx.lex.val = byte
                ctx.lex.type = LEX_STRING
                return ctx, 'STR'
            elseif byte == '/' then
                ctx.lex.val = byte
                return ctx, 'SLSH'
            elseif byte == '[' then
                return ctx, 'BRACE'
            else
                ctx.lex.val = byte
                return ctx, 'END'
            end
        end
        return ctx, 'START'
    end,
    IDENT = function(ctx, byte)
        if isalpha(byte) or isdigit(byte) or byte == '_' or byte == '.' then
            ctx.lex.val = ctx.lex.val .. byte
            return ctx, 'IDENT'
        else
            ctx.pos = ctx.pos - 1
            return ctx, 'END'
        end
    end,
    INTG = function(ctx, byte)
        if isdigit(byte) and ctx.lex.val ~= '0' then
            ctx.lex.val = ctx.lex.val .. byte
            return ctx, 'INTG'
        elseif ctx.lex.val == '0' then
            if isdigit(byte) then
                ctx.pos = ctx.pos - 1
                ctx.lex.val = 0
                ctx.lex.type = 'octal'
                return ctx, 'OCTAL'
            elseif byte == 'x' then
                ctx.lex.val = ''
                ctx.lex.type = 'hex'
                return ctx, 'HEX'
            end
        elseif byte == '.' then
            ctx.lex.val = ctx.lex.val .. byte
            ctx.lex.type = 'float'
            return ctx, 'FLOAT'
        elseif isalpha(byte) then
            error(('A number: %d, contains letter: %s'):format(
                ctx.lex.val, byte))
        end
        ctx.pos = ctx.pos - 1
        return ctx, 'END'
    end,
    OCTAL = function(ctx, byte)
        if isoctal(byte) then
            ctx.lex.val = ctx.lex.val .. byte
            return ctx, 'OCTAL'
        elseif isspace(byte) or byte == ';' then
            ctx.pos = ctx.pos - 1
            return ctx, 'END'
        else
            error(('Incorrect digit: %s for octal value: %s'):format(
                byte, ctx.lex.val))
        end
    end,
    HEX = function(ctx, byte)
        if ishex(byte) then
            ctx.lex.val = ctx.lex.val .. byte
            return ctx, 'HEX'
        elseif isspace(byte) or byte == ';' then
            ctx.pos = ctx.pos - 1
            return ctx, 'END'
        else
            error(('Incorrect digit: %s for hex value: %s'):format(
                byte, ctx.lex.val))
        end
    end,
    FLOAT = function(ctx, byte)
        if isdigit(byte) then
            ctx.lex.val = ctx.lex.val .. byte
            return ctx, 'FLOAT'
        elseif isspace(byte) or byte == ';' then
            ctx.pos = ctx.pos - 1
            return ctx, 'END'
        else
            error(('Incorrect digit: %s for float value: %f'):format(
                byte, ctx.lex.val))
        end
    end,
    STR = function(ctx, byte)
        if (byte == '"' or byte == "'") and ctx.lex.val:sub(1, 1) == byte and
            ctx.lex.val:sub(-1) ~= '\\' then
                ctx.lex.val = ctx.lex.val:sub(2)
                return ctx, 'END'
        else
            ctx.lex.val = ctx.lex.val .. byte
            return ctx, 'STR'
        end
    end,
    SLSH = function(ctx, byte)
        if byte == '/' then
            while byte ~= '\n' do
                byte, ctx = get_byte(ctx)
            end
            return ctx, 'START'
        else
            ctx.pos = ctx.pos - 1
            return ctx, 'END'
        end
    end,
    BRACE = function(ctx,byte)
        while byte ~= ']' do
            byte, ctx = get_byte(ctx)
        end
        return ctx, 'START'
    end,
}

local function get_lex(ctx)
    local state = 'START'
    while not feof(ctx) and state ~= 'END' do
        local byte
        byte, ctx = get_byte(ctx)
        ctx, state = case[state](ctx, byte)
    end
    if ctx.lex.type == 'ident' then
        if keywords.supported[ctx.lex.val] ~= nil then
            ctx.lex.type = keywords.supported[ctx.lex.val]
        elseif keywords.unsupported[ctx.lex.val] ~= nil then
            ctx.lex.type = keywords.unsupported[ctx.lex.val]
        elseif keywords.ignored[ctx.lex.val] ~= nil then
            ctx.lex.type = 'ignored'
        end
    elseif ctx.lex.type == 'octal' then
        ctx.lex.type = 'integer'
        ctx.lex.val = tonumber(ctx.lex.val, 8)
    elseif ctx.lex.type == 'hex' then
        ctx.lex.type = 'integer'
        ctx.lex.val = tonumber(ctx.lex.val, 16)
    elseif ctx.lex.type == 'float' or ctx.lex.type == 'integer' then
        ctx.lex.val = tonumber(ctx.lex.val)
    end
end

-- }}} Lexer

-- {{{ Parser

local function get_package_name(ctx)
    local package_declared = false
    local package_name
    while not feof(ctx) do
        get_lex(ctx)
        if ctx.lex.type == LEX_PACKAGE then
            if package_declared then
                error('Package can be declared only once')
            end
            package_declared = true
            get_lex(ctx)
            validate_name(ctx, 'Package')
            package_name = ctx.lex.val
            get_lex(ctx)
            check_punctuation(ctx, ';', 'Expected ; at the end of the line')
        end
    end
    ctx.pos = 1
    ctx.lex = {}
    return package_name, ctx
end

local function syntax_level(ctx)
    if ctx.lex.type ~= LEX_SYNTAX then
        error('Syntax level must be defined as first declaration in file')
    end
    get_lex(ctx)
    check_punctuation(ctx, '=', 'Missing = after "syntax"')
    get_lex(ctx)
    if ctx.lex.type ~= LEX_STRING then
        error('Syntax level must be defined by string value')
    end
    if ctx.lex.val ~= 'proto3' then
        error('Only proto3 syntax level is supported')
    end
    get_lex(ctx)
    check_punctuation(ctx, ';', 'Expected ; at the end of the line')
end

local function parse_enum_field(ctx, protocol, enum_name)
    local value, id
    validate_value_name(protocol, enum_name, ctx)
    value = ctx.lex.val
    get_lex(ctx)
    check_punctuation(ctx, '=',
        ('Expected = after the field: %s'):format(value))
    get_lex(ctx)
    validate_enum_id(protocol, enum_name, ctx, value)
    id = ctx.lex.val
    protocol[enum_name]['id_by_value'][value] = id
    protocol[enum_name]['value_by_id'][id] = value
    get_lex(ctx)
    check_punctuation(ctx, ';', 'Expected ; at the end of the line')
end

local function parse_message_field(ctx, protocol, message_name, forward_dec,
    is_oneof)
    local field_def = {}
    if ctx.lex.type == LEX_MESSAGE or ctx.lex.type == LEX_ONEOF then
        parse_message(ctx, protocol, forward_dec, message_name)
        return
    elseif ctx.lex.type == LEX_ENUM then
        get_lex(ctx)
        parse_enum(ctx, protocol, forward_dec, message_name)
        return
    elseif ctx.lex.type == LEX_REPEATED then
        if is_oneof then
            error(('%s oneof contains repeated field'):format(message_name))
        end
        get_lex(ctx)
        field_def.repeated = true
    elseif ctx.lex.type == LEX_MAP then
        parse_map(ctx, protocol, forward_dec, message_name)
        return
    end
    if ctx.lex.val == 'optional' then
        get_lex(ctx)
    end
    validate_field_type(ctx, protocol, forward_dec)
    if protocol[ctx.lex.val] ~= nil and protocol[ctx.lex.val]['map_entry'] then
        error(('The synthetic map entry message %s may not be directly ' ..
            'referenced by other field definitions'):format(ctx.lex.val))
    end
    field_def.type = ctx.lex.val
    get_lex(ctx)
    validate_field_name(protocol, message_name, ctx, 'Field')
    field_def.name = message_name .. '.' .. ctx.lex.val
    get_lex(ctx)
    check_punctuation(ctx, '=',
        ('Expected = after the field: %s'):format(field_def.name))
    get_lex(ctx)
    validate_message_id(protocol, message_name, ctx, field_def.name)
    field_def.id = ctx.lex.val
    protocol[message_name]['field_by_name'][field_def.name] = field_def
    protocol[message_name]['field_by_id'][field_def.id] = field_def
    get_lex(ctx)
    check_punctuation(ctx, ';', 'Expected ; at the end of the line')
end

parse_map = function(ctx, protocol, forward_dec, path)
    local key_type, value_type, map_name, map_id
    get_lex(ctx)
    check_punctuation(ctx, '<', 'Expected "<" before map types declaration')
    get_lex(ctx)
    if type(ctx.lex.type) ~= 'number' then
        error(('Key type for map can`t be non-scalar type, got: %s'):format(
            ctx.lex.val))
    elseif ctx.lex.type < LEX_INT32 or ctx.lex.type > LEX_STRING then
        error(('Key type has to be from allowed scalar key_types, ' ..
            'got: %s'):format(ctx.lex.val))
    end
    key_type = ctx.lex.val
    get_lex(ctx)
    check_punctuation(ctx, ',',
        'Expected "," between types in map daclaration')
    get_lex(ctx)
    validate_field_type(ctx, protocol, forward_dec)
    value_type = ctx.lex.val
    get_lex(ctx)
    check_punctuation(ctx, '>', 'Expected ">" after map types declaration')
    get_lex(ctx)
    validate_field_name(protocol, path, ctx, 'Map')
    map_name = ctx.lex.val
    get_lex(ctx)
    check_punctuation(ctx, '=', 'Expected "=" after map name')
    get_lex(ctx)
    validate_message_id(protocol, path, ctx, map_name)
    map_id = ctx.lex.val
    local nested_message_name = path .. '.' .. PascalCase(map_name) .. 'Entry'
    map_name = path .. '.' .. map_name
    protocol[nested_message_name] = {
        type = 'message',
        name = nested_message_name,
        map_entry = true,
        field_by_name = {
            key = {type = key_type, name = 'key', id = 1},
            value = {type = value_type, name = 'value', id = 2},
        },
        field_by_id = {
            [1] = {type = key_type, name = 'key', id = 1},
            [2] = {type = value_type, name = 'value', id = 2},
        },
    }
    local field_def = {type = nested_message_name, repeated = true,
        name = map_name, id = map_id}
    protocol[path]['field_by_name'][field_def.name] = field_def
    protocol[path]['field_by_id'][field_def.id] = field_def
    get_lex(ctx)
    check_punctuation(ctx, ';', 'Expected ; at the end of the line')
end

parse_message = function(ctx, protocol, forward_dec, path)
    local is_oneof = ctx.lex.type == LEX_ONEOF
    get_lex(ctx)
    if is_oneof then
        ctx.lex.val = ''
    end
    validate_name(ctx, 'Message', protocol)
    local message_name = ctx.lex.val
    if path ~= nil and message_name ~= '' then
        message_name = path .. '.' .. message_name
    elseif message_name == '' then
        message_name = path
    end
    protocol[message_name] = {
        type = 'message',
        name = message_name,
        field_by_name = {},
        field_by_id = {},
    }
    if forward_dec[message_name] then
        forward_dec[message_name] = nil
    end
    get_lex(ctx)
    check_punctuation(ctx, '{', 'Expected { after message name')
    get_lex(ctx)
    while ctx.lex.val ~= '}' do
        parse_message_field(ctx, protocol, message_name, forward_dec,
            is_oneof)
        get_lex(ctx)
    end
end

parse_enum = function(ctx, protocol, forward_dec, path)
    validate_name(ctx, 'Enum', protocol)
    local enum_name = ctx.lex.val
    if path ~= nil then
        enum_name = path .. '.' .. enum_name
    end
    protocol[enum_name] = {
        type = 'enum',
        name = enum_name,
        id_by_value = {},
        value_by_id = {},
    }
    if forward_dec[enum_name] then
        forward_dec[enum_name] = nil
    end
    get_lex(ctx)
    check_punctuation(ctx, '{', 'Expected { after message name')
    get_lex(ctx)
    while ctx.lex.val ~= '}' do
        parse_enum_field(ctx, protocol, enum_name)
        get_lex(ctx)
    end
    if protocol[enum_name]['value_by_id'][0] == nil then
        error(('Enum %s does not contain field with id 0'):format(enum_name))
    end
end

local function parse_import(ctx, import)
    local specifier = 'public'
    if ctx.lex.type == LEX_WEAK or ctx.lex.type == LEX_PUBLIC then
        specifier = ctx.lex.val
        get_lex(ctx)
    end
    if ctx.lex.type ~= LEX_STRING then
        error('Import name must be defined by string value')
    end
    import[specifier] = ctx.lex.val
    get_lex(ctx)
    check_punctuation(ctx, ';', 'Expected ; at the end of the line')
end

local function parser(fh)
    local ctx = {buffer = fh:read(), pos = 1, lex = {}}
    local forward_dec = {}
    local protocol = {}
    local import = {}
    local package_name
    package_name, ctx = get_package_name(ctx)
    get_lex(ctx)
    syntax_level(ctx)
    while not feof(ctx) do
        get_lex(ctx)
        if ctx.lex.type == LEX_MESSAGE then
            parse_message(ctx, protocol, forward_dec, package_name)
        elseif ctx.lex.type == LEX_ENUM then
            get_lex(ctx)
            parse_enum(ctx, protocol, forward_dec, package_name)
        elseif ctx.lex.type == LEX_IMPORT then
            get_lex(ctx)
            parse_import(ctx, import)
        elseif ctx.lex.type == 'ignored' or ctx.lex.type == LEX_PACKAGE then
            while ctx.lex.val ~= ';' do
                get_lex(ctx)
            end
        elseif ctx.lex.val ~= nil then
            error(('Unsupported top level structure: %s'):format(ctx.lex.val))
        end
    end
    return protocol, forward_dec, import
end

-- }}} Parser

return{
    parser = parser,
}
