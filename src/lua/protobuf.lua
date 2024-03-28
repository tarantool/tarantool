local ffi = require('ffi')
local encoder = require('internal.protobuf.wireformat')
local protocol_mt

local min_field_id = 1
local reserved_field_id_min = 19000
local reserved_field_id_max = 19999
local max_field_id = 2^29 - 1
local max_int64 = 2^64
local max_int32 = 2^32

local protobuf_types = {
    double = true,
    float = true,
    int32 = true,
    int64 = true,
    uint32 = true,
    uint64 = true,
    sint32 = true,
    sint64 = true,
    fixed32 = true,
    fixed64 = true,
    sfixed32 = true,
    sfixed64 = true,
    bool = true,
    string = true,
    bytes = true,
}

local protobuf_varint_types = {
    int32 = true,
    int64 = true,
    uint32 = true,
    uint64 = true,
    sint32 = true,
    sint64 = true,
    bool = true,
    enum = true,
}

local protobuf_len_types = {
    string = true,
    bytes = true,
    message = true,
}

local protobuf_I32_types = {
    fixed32 = true,
    sfixed32 = true,
}

local protobuf_I64_types = {
    fixed64 = true,
    sfixed64 = true,
}

local c_integer = {
    ['ctype<int64_t>'] = true,
    ['ctype<uint64_t>'] = true,
}

local input_varint_types = {
    boolean = true,
    number = true,
    cdata = true,
}

local input_len_types = {
    string = true,
}

local input_I32_types = {
    number = true,
    cdata = true,
}

local input_I64_types = {
    number = true,
    cdata = true
}

-- Create a message object suitable to pass
-- into the protobuf.protocol function.
--
-- Accepts a name of the message and a message
-- definition in the following format.
--
-- message_def = {
--    <field_name> = {<field_type>, <field_id>},
--    <...>
-- }
local function message(message_name, message_def)
    local field_by_name = {}
    local field_by_id = {}
    for field_name, def in pairs(message_def) do
        local field_type = def[1]
        local field_id = def[2]
        if field_by_id[field_id] ~= nil then
            error(('Id %d in field %q was already used'):format(field_id,
                field_name))
        end
        if field_id < min_field_id or field_id > max_field_id then
            error(('Id %d in field %q is out of range [%d; %d]'):format(
                field_id, field_name, min_field_id, max_field_id))
        end
        if field_id >= reserved_field_id_min and
           field_id <= reserved_field_id_max then
           error(('Id %d in field %q is in reserved ' ..
               'id range [%d, %d]'):format(field_id, field_name,
               reserved_field_id_min, reserved_field_id_max))
        end
        local field_def = {
            type = field_type,
            name = field_name,
            id = field_id,
        }
        field_by_name[field_name] = field_def
        field_by_id[field_id] = field_def
    end
    return {
        type = 'message',
        name = message_name,
        field_by_name = field_by_name,
        field_by_id = field_by_id
    }
end

-- Create a enum object suitable to pass into
-- the protobuf.protocol function.
--
-- Accepts a name of an enum and an enum definition
-- in the following format.
--
-- enum_def = {
--     <value_name> = <value_id>,
--     <...>
-- }
local function enum(enum_name, enum_def)
    local value_by_name = {}
    local value_by_id = {}
    for value_name, value_id in pairs(enum_def) do
        if value_by_id[value_id] ~= nil then
            error(('Double definition of enum field %q by %d'):format(
                value_name, value_id))
        end
        value_by_name[value_name] = value_id
        value_by_id[value_id] = value_name
    end
    return {
        type = 'enum',
        name = enum_name,
        value_by_name = value_by_name,
        value_by_id = value_by_id,
    }
end

-- Create a protocol object that stores message
-- data needed for encoding.
--
-- Accepts protocol definition using protobuf.message
-- and protobuf.enum functions as in example.
--
-- protocol_def = {
--     protocol.message(<message_name>, <message_def>),
--     protocol.enum(<enumc_name>, <enum_def>),
--     <...>
-- }
local function protocol(protocol_def)
    local return_protocol = {}
    local declarations = {}
    for _, def in ipairs(protocol_def) do
        local field_name = def['name']
        local field_type = def['type']
        if declarations[field_name] then
            error(('Double definition of name %q'):format(field_name))
        end
        if field_type == 'message' then
            for _, val in ipairs(def['field_by_id']) do
                local val_type = val['type']
                local standard = protobuf_types[val_type]
                local declared = declarations[val_type]
                if val_type == field_name then
                    error(('Message %q has a field of %q type ' ..
                        'Recursive definition is not allowed'):format(
                        field_name, val_type))
                elseif not standard and not declared then
                    declarations[val_type] = false
                end
            end
        end
        declarations[field_name] = true
        return_protocol[field_name] = def
    end
    for field_type, val in pairs(declarations) do
        if not val then
            error(('Type %q is not declared'):format(field_type))
        end
    end
    setmetatable(return_protocol, protocol_mt)
    return return_protocol
end

local function check_integer(data_spec, value)
    if type(value) == 'number' and math.ceil(value) ~= value then
        error(('Input number value %f for %q is not integer'):format(
            value, data_spec['name']))
    elseif (type(value) == 'cdata' and
            not c_integer[tostring(ffi.typeof(value))]) then
        error(('Input cdata value %q for %q field is not integer'):format(
            tostring(ffi.typeof(value)), data_spec['name']))
    end
end

local function code(data_spec, value)
    local data_type = data_spec['type']
    if (protobuf_varint_types[data_type] and
        input_varint_types[type(value)]) then
        check_integer(data_spec, value)
        return encoder.encode_integer(data_spec, value)
    elseif (protobuf_len_types[data_type] and
            input_len_types[type(value)]) then
        return encoder.encode_len(data_spec, value)
    elseif (data_type == 'float' and type(value) == 'number') then
        return encoder.encode_I32(data_spec, value)
    elseif (data_type == 'double' and type(value) == 'number') then
        return encoder.encode_I64(data_spec, value)
    elseif (protobuf_I32_types[data_type] and
            input_I32_types[type(value)]) then
        if (type(value) == 'number' and (value > max_int32-1 or
                value < -max_int32)) then
            error(('Input number value %q for %q field does ' ..
                'not fit in int32'):format(value, data_spec['name']))
        end
        check_integer(data_spec, value)
        return encoder.encode_I32(data_spec, value)
    elseif (protobuf_I64_types[data_type] and
            input_I64_types[type(value)]) then
        if (type(value) == 'number' and (value >= max_int64 or
                value < -max_int64)) then
            error(('Input number value %q for %q field does ' ..
                'not fit in int64'):format(value, data_spec['name']))
        end
        check_integer(data_spec, value)
        return encoder.encode_I64(data_spec, value)
    else
        error(('Field %q of %q type gets %q type value. ' ..
            'Unsupported or colliding types'):format(data_spec['name'],
            data_type, type(value)))
    end
end

-- Encodes the entered data in accordance with the
-- selected protocol into binary format.
--
-- Accepts a protocol created by protobuf.protocol function,
-- a name of a message selected for encoding and
-- the data that needs to be encoded in the following format.
--
-- data = {
--     <field_name> = <value>,
--     <...>
-- }
local function encode(protocol, name, data)
    local coded_data = {}
    local unknown_fields = {}
    local unknown_fields_counter = 0
    local message_protocol = protocol[name]
    if message_protocol == nil then
        error(('Wrong message name %q for this protocol'):format(name))
    end
    local fields = message_protocol['field_by_name']
    for field_name, value in pairs(data) do
        if fields[field_name] == nil then
            local data_spec = {type = 'bytes', id = unknown_fields_counter}
            unknown_fields_counter = unknown_fields_counter + 1
            table.insert(unknown_fields, code(data_spec, tostring(value)))
        else
            local field_type = fields[field_name]['type']
            local field_num = fields[field_name]['id']
            if protobuf_types[field_type] then
                table.insert(coded_data, code(fields[field_name], value))
            elseif protocol[field_type]['type'] == 'enum' then
                local values = protocol[field_type]['value_by_name']
                if type(value) ~= 'number' and values[value] == nil then
                    error(('%q is not defined in %q enum'):format(value,
                        field_type))
                end
                local data_spec = {type = 'int32', id = field_num}
                if type(value) == 'number' then
                    table.insert(coded_data, code(data_spec, value))
                else
                    table.insert(coded_data, code(data_spec, values[value]))
                end
            else
                local coded_msg = encode(protocol, field_type, value)
                local data_spec = {type = 'message', id = field_num}
                table.insert(coded_data, code(data_spec, coded_msg))
            end
        end
    end
    table.insert(coded_data, table.concat(unknown_fields))
    return table.concat(coded_data)
end

protocol_mt = {
    __index = {
        encode = encode,
    }
}

return {
    message = message,
    enum = enum,
    protocol = protocol,
    encode = encode,
}
