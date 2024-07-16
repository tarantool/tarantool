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

-- Forward declarations
local decode_int

-- {{{ Helpers

-- 32-bit IEEE 754 representation of the given number.
local function as_float(value)
    local p = ffi.new('float[1]')
    p[0] = value
    return ffi.string(ffi.cast('char *', p), 4)
end

-- Number representation of IEEE 754 32-bit value.
local function from_float(value)
    return ffi.cast('float *', value)[0]
end

-- 64-bit IEEE 754 representation of the given number.
local function as_double(value)
    local p = ffi.new('double[1]')
    p[0] = value
    return ffi.string(ffi.cast('char *', p), 8)
end

-- Number representation of IEEE 754 64-bit value.
local function from_double(value)
    return ffi.cast('double *', value)[0]
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

-- Number representation of 32-bit two's complement value.
local function from_int32(value)
    return ffi.cast('uint32_t *', value)[0]
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

-- Number representation of 64-bit two's complement value.
local function from_int64(value)
    return ffi.cast('uint64_t *', value)[0]
end

-- Converts input unsigned cdata into signed cdata or lua number if
-- it fits lua number integer representation without rounding range.
local function cast_signed_type(value)
    assert(type(value) == 'cdata')
    value = ffi.cast('int64_t', value)
    if value < 2LL^53 - 1 and value > -2LL^53 + 1 then
        value = tonumber(value)
    end
    return value
end

-- Converts input unsigned cdata into lua number if it fits lua
-- number integer representation without rounding range.
local function cast_unsigned_type(value)
    assert(type(value) == 'cdata')
    if value < 2ULL^53 - 1 then
        value = tonumber(value)
    end
    return value
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

-- Decode input value as VARINT without a tag.
--
-- Input value type: string
--
-- This is a helper function to decode tag and data values.
local function decode_varint(ctx)
    local msb
    local res = 0
    local len = 0
    repeat
        local payload = ffi.cast('uint64_t', string.byte(ctx.val, ctx.pos))
        if len >= 10 or payload == nil then
            error('Incorrect msb for varint to decode')
        end
        msb = bit.band(payload, 0x80)
        payload = bit.band(payload, 0x7f)
        res = bit.bor(res, bit.lshift(payload, 7 * len))
        ctx.pos = ctx.pos + 1
        len = len + 1
    until msb == 0
    return res, ctx
end

-- Encode a tag byte.
--
-- Tag byte consists of the given field_id and the given Protocol Buffers
-- wire type. This is the first byte of Tag-Length-Value encoding.
local function encode_tag(field_id, wire_type)
    assert(wire_type >= 0 and wire_type <= 5)
    return encode_varint(bit.bor(bit.lshift(field_id, 3), wire_type))
end

-- Decode a tag byte.
local function decode_tag(ctx)
    local tag, ctx = decode_int(ctx)
    return bit.band(tag, 0x7), bit.rshift(tag, 3), ctx
end

-- Return default value for input type.
local function set_default(type)
    assert(type ~= 'message')
    if type == 'bool' then return BOOL_DEFAULT
    elseif type == 'string' or type == 'bytes' then return STRING_DEFAULT
    else return NUMERIC_DEFAULT
    end
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

-- Decode an integral value encoded as VARINT using two complement encoding.
--
-- Input value types: string.
--
-- Used for Protocol Buffers types: int32, int64.
decode_int = function(ctx)
    local res, ctx = decode_varint(ctx)
    res = cast_signed_type(res)
    return res, ctx
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

-- Decode an integral value encoded as VARINT using the "ZigZag" encoding.
--
-- Input value types: string
--
-- Used for Protocol Buffers types: sint32, sint64.
local function decode_sint(ctx)
    local res, ctx = decode_varint(ctx)
    if res % 2 == 0 then
        res = res / 2
    else
        res = -(res / 2) - 1
    end
    res = cast_signed_type(res)
    return res, ctx
end

-- Decode an integral value encoded as VARINT using two complement encoding.
--
-- Input value types: string.
--
-- Used for Protocol Buffers types: uint32, uint64.
local function decode_uint(ctx)
    local res, ctx = decode_varint(ctx)
    res = cast_unsigned_type(res)
    return res, ctx
end

-- Decode an boolean value encoded as VARINT using two complement encoding.
--
-- Input value type: string.
--
-- Used for Protocol Buffers types: bool.
local function decode_bool(ctx)
    local payload, ctx = decode_varint(ctx)
    if payload == 1 then
        return true, ctx
    else
        return false, ctx
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

-- Decode an unsigned integral value encoded as I32.
--
-- Input value type: string.
--
-- Used for Protocol Buffers types: fixed32.
local function decode_fixed32(ctx)
    local res = string.sub(ctx.val, ctx.pos, ctx.pos + 3)
    ctx.pos = ctx.pos + 4
    return tonumber(from_int32(res)), ctx
end

-- Decode a signed integral value encoded as I32.
--
-- Input value type: string.
--
-- Used for Protocol Buffers types: sfixed32.
local function decode_sfixed32(ctx)
    local res = string.sub(ctx.val, ctx.pos, ctx.pos + 3)
    ctx.pos = ctx.pos + 4
    return tonumber(ffi.cast('int32_t', from_int32(res))), ctx
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

-- Decode a floating point value encoded as I32.
--
-- Input value type: number.
--
-- Used for Protocol Buffers type: float.
local function decode_float(ctx)
    local res = string.sub(ctx.val, ctx.pos, ctx.pos + 3)
    ctx.pos = ctx.pos + 4
    return from_float(res), ctx
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

-- Decode an unsigned integral value encoded as I64.
--
-- Input value type: string.
--
-- Used for Protocol Buffers types: fixed64.
local function decode_fixed64(ctx)
    local res = string.sub(ctx.val, ctx.pos, ctx.pos + 7)
    ctx.pos = ctx.pos + 8
    res = from_int64(res)
    res = cast_unsigned_type(res)
    return res, ctx
end

-- Decode a signed integral value encoded as I64.
--
-- Input value type: string.
--
-- Used for Protocol Buffers types: sfixed64.
local function decode_sfixed64(ctx)
    local res = string.sub(ctx.val, ctx.pos, ctx.pos + 7)
    ctx.pos = ctx.pos + 8
    res = from_int64(res)
    res = cast_signed_type(res)
    return res, ctx
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

-- Decode a floating point value encoded as I64.
--
-- Input value type: number.
--
-- Used for Protocol Buffers type: float.
local function decode_double(ctx)
    local res = string.sub(ctx.val, ctx.pos, ctx.pos + 7)
    ctx.pos = ctx.pos + 8
    return from_double(res), ctx
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

-- Decode a string value encoded as LEN.
--
-- Input value type: string.
--
-- Used for Protocol Buffers types: string, bytes, embedded message, packed
-- repeated fields.
local function decode_len(ctx)
    local len, ctx = decode_int(ctx)
    local res = string.sub(ctx.val, ctx.pos, ctx.pos + len - 1)
    ctx.pos = ctx.pos + len
    return res, ctx
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
    set_default = set_default,
    decode_tag = decode_tag,
    decode_int = decode_int,
    decode_sint = decode_sint,
    decode_uint = decode_uint,
    decode_bool = decode_bool,
    decode_float = decode_float,
    decode_double = decode_double,
    decode_fixed32 = decode_fixed32,
    decode_sfixed32 = decode_sfixed32,
    decode_fixed64 = decode_fixed64,
    decode_sfixed64 = decode_sfixed64,
    decode_len = decode_len,
}
