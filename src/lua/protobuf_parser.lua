local fio = require('fio')

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
local LEX_REPEATED = 6
local LEX_STRING = 7
local LEX_BYTES = 8
local LEX_INT32 = 9
local LEX_INT64 = 10
local LEX_UINT32 = 11
local LEX_UINT64 = 12
local LEX_SINT32 = 13
local LEX_SINT64 = 14
local LEX_FIXED32 = 15
local LEX_FIXED64 = 16
local LEX_SFIXED32 = 17
local LEX_SFIXED64 = 18
local LEX_BOOL = 19
local LEX_FLOAT = 20
local LEX_DOUBLE = 21

local keywords = {
    syntax = LEX_SYNTAX,
    message = LEX_MESSAGE,
    enum = LEX_ENUM,
    repeated = LEX_REPEATED,
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
}

local unsupported_keywords = {
    inf = 'inf',
    nan = 'nan',
    edition = 'edition',
    import = 'import',
    weak = 'weak',
    public = 'public',
    package = 'package',
    option = 'option',
    service = 'service',
    extend = 'extend',
    group = 'group',
    oneof = 'oneof',
    map = 'map',
    extensions = 'extensions',
    reserved = 'reserved',
    rpc = 'rpc',
    stream = 'stream',
    returns = 'returns',
    to = 'to',
    max = 'max',
    optional = 'optional',
    required = 'required',
}

-- Forward declarations
local parse_message
local parse_enum

--local LEX_FIN = 0
--local LEX_IDENT = 1
--local LEX_NUM = 2

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

local function validate_message_id(field_id, field_name)
    if field_id < MIN_FIELD_ID or field_id > MAX_FIELD_ID then
        error(('Id %d in field %q is out of range [%d; %d]'):format(
             field_id, field_name, MIN_FIELD_ID, MAX_FIELD_ID))
    end
    if field_id >= RESERVED_FIELD_ID_MIN and
        field_id <= RESERVED_FIELD_ID_MAX then
        error(('Id %d in field %q is in reserved ' ..
            'id range [%d, %d]'):format(field_id, field_name,
            RESERVED_FIELD_ID_MIN, RESERVED_FIELD_ID_MAX))
    end
end

local function validate_enum_id(enum_id, field_name)
    if enum_id < MIN_ENUM_ID or enum_id > MAX_ENUM_ID then
        error(('Id %d in field %q is out of range [%d; %d]'):format(
             enum_id, field_name, MIN_ENUM_ID, MAX_ENUM_ID))
    end
end

local function checktable(table)
    if next(table) == nil then
        return nil
    else
        local key, _ = next(table)
        return key
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
            else
                ctx.lex.val = byte
                return ctx, 'END'
            end
        end
        return ctx, 'START'
    end,
    IDENT = function(ctx, byte)
        if isalpha(byte) or isdigit(byte) or byte == '_' then
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
}

local function get_lex(ctx)
    local state = 'START'
    while not feof(ctx) and state ~= 'END' do
        local byte
        byte, ctx = get_byte(ctx)
        ctx, state = case[state](ctx, byte)
    end
    if ctx.lex.type == 'ident' then
        if keywords[ctx.lex.val] ~= nil then
            ctx.lex.type = keywords[ctx.lex.val]
        elseif unsupported_keywords[ctx.lex.val] ~= nil then
            error(('Unsupported keyword: %s'):format(ctx.lex.val))
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

local function syntax_level(ctx)
    if ctx.lex.type ~= LEX_SYNTAX then
        error('Syntax level must be defined as first declaration in file')
    end
    get_lex(ctx)
    if ctx.lex.val ~= '=' then
        error('Missing = after "syntax"')
    end
    get_lex(ctx)
    if ctx.lex.type ~= LEX_STRING then
        error('Syntax level must be defined by string value')
    end
    if ctx.lex.val ~= 'proto3' then
        error('Only proto3 syntax level is supported')
    end
    get_lex(ctx)
    if ctx.lex.val ~= ';' then
        error('Expected ; at the end of the line')
    end
end

local function parse_enum_field(ctx, protocol, enum_name)
    local value, id
    if ctx.lex.type ~= 'ident' then
        error(('Field name must be identificator, got: %s'):format(
            ctx.lex.val))
    end
    value = ctx.lex.val
    get_lex(ctx)
    if ctx.lex.val ~= '=' then
        error(('Expected = after the field: %s'):format(value))
    end
    get_lex(ctx)
    if ctx.lex.type ~= 'integer' then
        error(('Enum value must be integer, got: %s'):format(ctx.lex.val))
    end
    validate_enum_id(ctx.lex.val, value)
    id = ctx.lex.val
    if protocol[enum_name]['id_by_value'][value] ~= nil then
        error(('Value "%s" isn`t unique in "%s" enum'):format(
            value, enum_name))
    end
    protocol[enum_name]['id_by_value'][value] = id
    if protocol[enum_name]['value_by_id'][id] ~= nil then
        error(('Value id %d isn`t unique in "%s" enum'):format(
            id, enum_name))
    end
    protocol[enum_name]['value_by_id'][id] = value
    get_lex(ctx)
    if ctx.lex.val ~= ';' then
        error('Expected ; at the end of the line')
    end
end

local function parse_message_field(ctx, protocol, message_name, forward_dec)
    local field_def = {}
    if ctx.lex.type == LEX_MESSAGE then
        get_lex(ctx)
        parse_message(ctx, protocol, forward_dec)
        return
    elseif ctx.lex.type == LEX_ENUM then
        get_lex(ctx)
        parse_enum(ctx, protocol, forward_dec)
        return
    elseif ctx.lex.type == LEX_REPEATED then
        get_lex(ctx)
        field_def.repeated = true
    end
    if keywords[ctx.lex.val] ~= nil and ctx.lex.type <= LEX_REPEATED then
        error(('Incorrect type for field: %s'):format(ctx.lex.val))
    elseif unsupported_keywords[ctx.lex.val] ~= nil then
        error(('Unsupported keyword: %s can`t be a field type'):format(
            ctx.lex.val))
    elseif keywords[ctx.lex.val] == nil and protocol[ctx.lex.val] == nil then
        forward_dec[ctx.lex.val] = true
    end
    field_def.type = ctx.lex.val
    get_lex(ctx)
    if ctx.lex.type ~= 'ident' then
        error(('Field name must be identificator, got: %s'):format(
            ctx.lex.val))
    end
    field_def.name = ctx.lex.val
    get_lex(ctx)
    if ctx.lex.val ~= '=' then
        error(('Expected = after the field: %s'):format(field_def.name))
    end
    get_lex(ctx)
    if ctx.lex.type ~= 'integer' then
        error(('Field id must be integer, got: %s'):format(ctx.lex.val))
    end
    validate_message_id(ctx.lex.val, field_def.name)
    field_def.id = ctx.lex.val
    if protocol[message_name]['field_by_name'][field_def.name] ~= nil then
        error(('Field name "%s" isn`t unique in "%s" message'):format(
            field_def.name, message_name))
    end
    protocol[message_name]['field_by_name'][field_def.name] = field_def
    if protocol[message_name]['field_by_id'][field_def.id] ~= nil then
        error(('Field id %d isn`t unique in "%s" message'):format(
            field_def.id, message_name))
    end
    protocol[message_name]['field_by_id'][field_def.id] = field_def
    get_lex(ctx)
    if ctx.lex.val ~= ';' then
        error('Expected ; at the end of the line')
    end
end

parse_message = function(ctx, protocol, forward_dec)
    if ctx.lex.type ~= 'ident' then
        error(('Message name must be identificator, got: %s'):format(
            ctx.lex.val))
    end
    if protocol[ctx.lex.val] ~= nil then
        error(('Object with same name: %s has been already declared'):format(
            ctx.lex.val))
    end
    local message_name = ctx.lex.val
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
    if ctx.lex.val ~= '{' then
        error('Expected { after message name')
    end
    get_lex(ctx)
    while ctx.lex.val ~= '}' do
        parse_message_field(ctx, protocol, message_name, forward_dec)
        get_lex(ctx)
    end
end

parse_enum = function(ctx, protocol, forward_dec)
    if ctx.lex.type ~= 'ident' then
        error(('Enum name must be identificator, got: %s'):format(
            ctx.lex.val))
    end
    if protocol[ctx.lex.val] ~= nil then
        error(('Object with same name: %s has been already declared'):format(
            ctx.lex.val))
    end
    local enum_name = ctx.lex.val
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
    if ctx.lex.val ~= '{' then
        error('Expected { after enum name')
    end
    get_lex(ctx)
    while ctx.lex.val ~= '}' do
        parse_enum_field(ctx, protocol, enum_name)
        get_lex(ctx)
    end
    if protocol[enum_name]['value_by_id'][0] == nil then
        error(('Enum %s does not contain field with id 0'):format(enum_name))
    end
end

local function parser(filename)
    local fh = fio.open(filename, 'O_RDONLY')
    local ctx = {buffer = fh:read(), pos = 1, lex = {}}
    local forward_dec = {}
    local protocol = {}
    get_lex(ctx)
    syntax_level(ctx)
    while not feof(ctx) do
        get_lex(ctx)
        if ctx.lex.type == LEX_MESSAGE then
            get_lex(ctx)
            parse_message(ctx, protocol, forward_dec)
        elseif ctx.lex.type == LEX_ENUM then
            get_lex(ctx)
            parse_enum(ctx, protocol, forward_dec)
        end
    end
    local undeclared = checktable(forward_dec)
    if undeclared ~= nil then
        error(('%s declaration is missing in protocol'):format(undeclared))
    end
    return protocol
end

-- }}} Parser

return{
    parser = parser,
}
