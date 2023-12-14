local ffi = require('ffi')
local max_unsigned = 18446744073709551615
local max_signed = 9223372036854775807
local max_uint32 = 4294967295ULL
local min_int32 = -2147483648ULL

local signed_int_32 = {
    int32 = true,
    sint32 = true,
}

local unsigned_int_32 = {
    uint32 = true,
}

local unsigned_int = {
    uint32 = true,
    uint64 = true,
}

local signed_int = {
    sint32 = true,
    sint64 = true,
}

local int = {
    int32 = true,
    int64 = true,
}

local function encode_uint(data)
    local code = ''
    local byte
    data = ffi.cast('uint64_t', data)
    repeat
        byte = (data % 128) + 128
        data = data / 128
        if data == 0 then
            byte = byte - 128
        end
        code = code .. string.char(tonumber(byte))
    until data == 0
    return code
end

local function encode_sint(data, field_num)
    if data > 0 then
        return encode_uint(field_num) .. encode_uint(2 * data)
    else
        return encode_uint(field_num) .. encode_uint(2 * (-data) - 1)
    end
end

local function encode_float(data)
    local p = ffi.new('float[1]')
    p[0] = data
    return ffi.string(ffi.cast('char*', p), 4)
end

local function encode_double(data)
    local p = ffi.new('double[1]')
    p[0] = data
    return ffi.string(ffi.cast('char*', p), 8)
end

local function encode_fixed32(data)
    local code = ''
    local byte
    data = ffi.cast('uint32_t', data)
    repeat
        byte = data % 256
        data = data / 256
        code = code .. string.char(tonumber(byte))
    until string.len(code) == 4
    return code
end

local function encode_fixed64(data)
    local code = ''
    local byte
    data = ffi.cast('uint64_t', data)
    repeat
        byte = data % 256
        data = data / 256
        code = code .. string.char(tonumber(byte))
    until string.len(code) == 8
    return code
end

local function encode_integer(data_spec, data)
    local data_type = data_spec['type']
    local field_id = data_spec['id']
    local field_num = bit.lshift(field_id, 3)
    if type(data) == 'boolean' and data then
        return encode_uint(field_num) .. string.char(1)
    elseif type(data) == 'boolean' then
        return encode_uint(field_num) .. string.char(0)
    end
    if data > 2^32 - 1 and unsigned_int_32[data_type] then
        error(('Input data for %q field is %d and do not fit in ' ..
            'uint_32'):format(data_spec['name'], tonumber(data)))
    elseif data > 2^31 - 1 and signed_int_32[data_type] then
        error(('Input data for %q field is %d and do not fit in ' ..
            'sint_32'):format(data_spec['name'], tonumber(data)))
    elseif data < -2^31 and signed_int_32[data_type] then
        error(('Input data for %q field is %d and do not fit in ' ..
            'sint_32'):format(data_spec['name'], tonumber(data)))
    elseif data < 0 and unsigned_int[data_type] then
        error(('Input data for %q field is %d and do not fit in ' ..
            'unsigned type'):format(data_spec['name'], tonumber(data)))
    elseif data >= 0 then
        return encode_uint(field_num) .. encode_uint(data)
    elseif data < 0 and signed_int[data_type] then
        return encode_sint(data, field_num)
    elseif data < 0 and int[data_type] then
        return encode_uint(field_num) ..
               encode_uint(ffi.cast('uint64_t', data))
    end
end

local function encode_len(data_spec, data)
    local field_id = data_spec['id']
    local field_num = bit.lshift(field_id, 3)
    local str_len = string.len(data)
    if str_len > 2^32 then
        error(("Too long string in %q field"):format(data_spec['name']))
    else
        return string.format('%s%s%s',
                             encode_uint(field_num + 2),
                             encode_uint(str_len),
                             data)
    end
end

local function encode_I32(data_spec, data)
    local data_type = data_spec['type']
    local field_id = data_spec['id']
    local field_num = bit.lshift(field_id, 3)
    if data_type == 'float' then
        return encode_uint(field_num + 5) .. encode_float(data)
    end
    if type(data) == 'cdata' and (data <= max_uint32 or
        (data <= -1ULL and data >= min_int32)) then
        return encode_uint(field_num + 5) .. encode_fixed32(data)
    elseif type(data) == 'cdata' then
        error(('Input data for %q field is %s and do not fit in ' ..
            'int_32'):format(data_spec['name'], data))
    end
    if data_type == 'fixed32' and data > 2^32 - 1 then
        error(('Input data for %q field is %d and do not fit in ' ..
            'int_32'):format(data_spec['name'], data))
    elseif (data_type == 'sfixed32' and
            (data > 2^31 - 1 or data < -2^31)) then
        error(('Input data for %q field is %d and do not fit in ' ..
            'int_32'):format(data_spec['name'], data))
    elseif data_type == 'fixed32' and data < 0 then
        error(('Input data for %q field is %d and do not fit in ' ..
            'unsigned type'):format(data_spec['name'], data))
    end
    return encode_uint(field_num + 5) .. encode_fixed32(data)
end

local function encode_I64(data_spec, data)
    local data_type = data_spec['type']
    local field_id = data_spec['id']
    local field_num = bit.lshift(field_id, 3)
    if data_type == 'double' then
        return encode_uint(field_num + 1) .. encode_double(data)
    end
    if type(data) == 'cdata' then
        return encode_uint(field_num + 1) .. encode_fixed64(data)
    end
    if data_type == 'fixed64' and data > max_unsigned then
        error(('Input data for %q field is %d and do not fit in ' ..
            'int_64'):format(data_spec['name'], data))
    elseif (data_type == 'sfixed64' and
            (data > max_signed - 1 or data < -max_signed)) then
        error(('Input data for %q field is %d and do not fit in ' ..
            'sint_64'):format(data_spec['name'], data))
    elseif data_type == 'fixed64' and data < 0 then
        error(('Input data for %q field is %d and do not fit in ' ..
            'unsigned type'):format(data_spec['name'], data))
    end
    return encode_uint(field_num + 1)..encode_fixed64(data)
end

return{
    encode_integer = encode_integer,
    encode_len = encode_len,
    encode_I32 = encode_I32,
    encode_I64 = encode_I64,
}
