local ffi = require('ffi')
local int64_t = ffi.typeof('int64_t')
local uint64_t = ffi.typeof('uint64_t')

local WIRE_TYPE_VARINT = 0
local WIRE_TYPE_I64 = 1
local WIRE_TYPE_LEN = 2
-- SGROUP (3) and EGROUP (4) are deprecated in proto3.
local WIRE_TYPE_I32 = 5

local NUMERIC_DEFAULT = 0
local STRING_DEFAULT = ''
local BOOL_DEFAULT = false

-- {{{ Helpers

-- 32-bit IEEE 754 representation of the given number.
local function as_float(value)
    local p = ffi.new('float[1]')
    p[0] = value
    return ffi.string(ffi.cast('char *', p), 4)
end

-- 64-bit IEEE 754 representation of the given number.
local function as_double(value)
    local p = ffi.new('double[1]')
    p[0] = value
    return ffi.string(ffi.cast('char *', p), 8)
end

-- 32-bit two's complement representation of the given integral number.
local function as_int32(value)
    -- Set the type of storage for the given value: signed or unsigned 32-bit.
    local ctype
    if type(value) == 'number' then
        ctype = value < 0 and 'int32_t[1]' or 'uint32_t[1]'
    elseif type(value) == 'cdata' and ffi.istype(int64_t, value) then
        ctype = 'int32_t[1]'
    elseif type(value) == 'cdata' and ffi.istype(uint64_t, value) then
        ctype = 'uint32_t[1]'
    else
        assert(false)
    end
    local p = ffi.new(ctype)
    p[0] = value
    return ffi.string(ffi.cast('char *', p), 4)
end

-- 64-bit two's complement representation of the given integral number.
local function as_int64(value)
    local ctype
    -- Set the type of storage for the given value: signed or unsigned 64-bit.
    if type(value) == 'number' then
        ctype = value < 0 and 'int64_t[1]' or 'uint64_t[1]'
    elseif type(value) == 'cdata' and ffi.istype(int64_t, value) then
        ctype = 'int64_t[1]'
    elseif type(value) == 'cdata' and ffi.istype(uint64_t, value) then
        ctype = 'uint64_t[1]'
    else
        assert(false)
    end
    local p = ffi.new(ctype)
    p[0] = value
    return ffi.string(ffi.cast('char *', p), 8)
end

-- Encode an integral value as VARINT without a tag.
--
-- Input value types: number (integral), cdata<int64_t>, cdata<uint64_t>.
--
-- This is a helper function to encode tag and data values.
--
-- https://protobuf.dev/programming-guides/encoding/#varints
local function encode_varint(value)
    local buf = ffi.new('char[?]', 11)
    local size = 0
    -- The bit module defines bit arithmectic on the number type as 32 bit.
    -- We need to handle numbers beyond 2^53 so we use cast to cdata.
    --
    -- Note: casting a negative value to an unsigned type is an undefined
    -- behavior thus we cast it to a signed type.
    if type(value) == 'number' then
        local ctype = value < 0 and int64_t or uint64_t
        value = ffi.cast(ctype, value)
    end
    repeat
        -- Extract next 7 bit payload and add a continuation bit
        -- (set the most significant bit to 1).
        local payload = bit.bor(bit.band(value, 0x7f), 0x80)
        value = bit.rshift(value, 7)
        -- Write the payload and continuation bit to the buffer.
        buf[size] = payload
        size = size + 1
    until value == 0
    -- Set the continuation bit to zero for the last byte.
    buf[size-1] = bit.band(buf[size-1], 0x7f)
    return ffi.string(buf, size)
end

-- Encode a tag byte.
--
-- Tag byte consists of the given field_id and the given Protocol Buffers
-- wire type. This is the first byte of Tag-Length-Value encoding.
local function encode_tag(field_id, wire_type)
    assert(wire_type >= 0 and wire_type <= 5)
    return encode_varint(bit.bor(bit.lshift(field_id, 3), wire_type))
end

-- }}} Helpers

-- {{{ API functions

-- Encode an integral value as VARINT using two complement encoding.
--
-- Input value types: number (integral), cdata<int64_t>, cdata<uint64_t>,
-- boolean.
--
-- Used for Protocol Buffers types: int32, int64, uint32, uint64, bool, enum.
local function encode_int(field_id, value)
    if value == NUMERIC_DEFAULT or value == BOOL_DEFAULT then
        return ''
    end
    if type(value) == 'boolean' then
        value = value and 1 or 0
    end
    return encode_tag(field_id, WIRE_TYPE_VARINT) .. encode_varint(value)
end

-- Encode an integral value as VARINT using the "ZigZag" encoding.
--
-- Input value types: number (integral), cdata<int64_t>, cdata<uint64_t>.
--
-- Used for Protocol Buffers types: sint32, sint64.
local function encode_sint(field_id, value)
    if value >= 0 then
        return encode_int(field_id, 2 * value)
    else
        value = ffi.cast('uint64_t', -value)
        return encode_int(field_id, 2 * value - 1)
    end
end

-- Encode an integral value as I32.
--
-- Input value types: number (integral), cdata<int64_t>, cdata<uint64_t>.
--
-- Used for Protocol Buffers types: fixed32, sfixed32.
local function encode_fixed32(field_id, value)
    if value == NUMERIC_DEFAULT then
        return ''
    end
    return encode_tag(field_id, WIRE_TYPE_I32) .. as_int32(value)
end

-- Encode a floating point value as I32.
--
-- Input value type: number.
--
-- Used for Protocol Buffers type: float.
local function encode_float(field_id, value)
    if value == NUMERIC_DEFAULT then
        return ''
    end
    return encode_tag(field_id, WIRE_TYPE_I32) .. as_float(value)
end

-- Encode an integral value as I64.
--
-- Input value types: number (integral), cdata<int64_t>, cdata<uint64_t>.
--
-- Used for Protocol Buffers types: fixed64, sfixed64.
local function encode_fixed64(field_id, value)
    if value == NUMERIC_DEFAULT then
        return ''
    end
    return encode_tag(field_id, WIRE_TYPE_I64) .. as_int64(value)
end

-- Encode a floating point value as I64.
--
-- Input value type: number.
--
-- Used for Protocol Buffers type: double.
local function encode_double(field_id, value)
    if value == NUMERIC_DEFAULT then
        return ''
    end
    return encode_tag(field_id, WIRE_TYPE_I64) .. as_double(value)
end

-- Encode a string value as LEN.
--
-- Input value type: string. The string contains raw bytes to encode.
--
-- Used for Protocol Buffers types: string, bytes, embedded message, packed
-- repeated fields.
local function encode_len(field_id, value, ex_presence)
    if value == STRING_DEFAULT and not ex_presence then
        return ''
    end
    return string.format('%s%s%s',
        encode_tag(field_id, WIRE_TYPE_LEN),
        encode_varint(string.len(value)),
        value)
end

-- }}} API functions

return{
    encode_int = encode_int,
    encode_sint = encode_sint,
    encode_float = encode_float,
    encode_fixed32 = encode_fixed32,
    encode_double = encode_double,
    encode_fixed64 = encode_fixed64,
    encode_len = encode_len,
}
