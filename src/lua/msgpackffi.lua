-- msgpackffi.lua (internal file)

local ffi = require('ffi')
local buffer = require('buffer')
local builtin = ffi.C
local msgpack = require('msgpack') -- .NULL, .array_mt, .map_mt, .cfg
local int8_ptr_t = ffi.typeof('int8_t *')
local uint8_ptr_t = ffi.typeof('uint8_t *')
local uint16_ptr_t = ffi.typeof('uint16_t *')
local uint32_ptr_t = ffi.typeof('uint32_t *')
local uint64_ptr_t = ffi.typeof('uint64_t *')
local char_ptr_t = ffi.typeof('char *')
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_drop = buffer.internal.cord_ibuf_drop

ffi.cdef([[
char *
mp_encode_float(char *data, float num);
char *
mp_encode_double(char *data, double num);
char *
mp_encode_decimal(char *data, decimal_t *dec);
uint32_t
mp_sizeof_decimal(const decimal_t *dec);
char *
mp_encode_uuid(char *data, const struct tt_uuid *uuid);
uint32_t
mp_sizeof_uuid();
float
mp_decode_float(const char **data);
double
mp_decode_double(const char **data);
uint32_t
mp_decode_extl(const char **data, int8_t *type);
decimal_t *
decimal_unpack(const char **data, uint32_t len, decimal_t *dec);
struct tt_uuid *
uuid_unpack(const char **data, uint32_t len, struct tt_uuid *uuid);
]])

local strict_alignment = (jit.arch == 'arm')

local function bswap_u16(num)
    return bit.rshift(bit.bswap(tonumber(num)), 16)
end

--------------------------------------------------------------------------------
-- Encoder
--------------------------------------------------------------------------------

local encode_ext_cdata = {}

-- Set trigger that called when encoding cdata
local function on_encode(ctype_or_udataname, callback)
    if type(ctype_or_udataname) ~= "cdata" or type(callback) ~= "function" then
        error("Usage: on_encode(ffi.typeof('mytype'), function(buf, obj)")
    end
    local ctypeid = tonumber(ffi.typeof(ctype_or_udataname))
    local prev = encode_ext_cdata[ctypeid]
    encode_ext_cdata[ctypeid] = callback
    return prev
end

local function encode_fix(buf, code, num)
    local p = buf:alloc(1)
    p[0] = bit.bor(code, tonumber(num))
end

local function encode_u8(buf, code, num)
    local p = buf:alloc(2)
    p[0] = code
    ffi.cast(uint8_ptr_t, p + 1)[0] = num
end

local encode_u16
if strict_alignment then
    encode_u16 = function(buf, code, num)
        local u16 = ffi.new('uint16_t[1]', bswap_u16(num))
        local p = buf:alloc(3)
        p[0] = code
        ffi.copy(p + 1, u16, 2)
    end
else
    encode_u16 = function(buf, code, num)
        local p = buf:alloc(3)
        p[0] = code
        ffi.cast(uint16_ptr_t, p + 1)[0] = bswap_u16(num)
    end
end

local encode_u32
if strict_alignment then
    encode_u32 = function(buf, code, num)
        local u32 = ffi.new('uint32_t[1]',
                            ffi.cast('uint32_t', bit.bswap(tonumber(num))))
        local p = buf:alloc(5)
        p[0] = code
        ffi.copy(p + 1, u32, 4)
    end
else
    encode_u32 = function(buf, code, num)
        local p = buf:alloc(5)
        p[0] = code
        ffi.cast(uint32_ptr_t, p + 1)[0] =
            ffi.cast('uint32_t', bit.bswap(tonumber(num)))
    end
end

local encode_u64
if strict_alignment then
    encode_u64 = function(buf, code, num)
        local u64 = ffi.new('uint64_t[1]', bit.bswap(ffi.cast('uint64_t', num)))
        local p = buf:alloc(9)
        p[0] = code
        ffi.copy(p + 1, u64, 8)
    end
else
    encode_u64 = function(buf, code, num)
        local p = buf:alloc(9)
        p[0] = code
        ffi.cast(uint64_ptr_t, p + 1)[0] = bit.bswap(ffi.cast('uint64_t', num))
    end
end

local function encode_float(buf, num)
    local p = buf:alloc(5)
    builtin.mp_encode_float(p, num)
end

local function encode_double(buf, num)
    local p = buf:alloc(9)
    builtin.mp_encode_double(p, num)
end

local function encode_decimal(buf, num)
    local p = buf:alloc(builtin.mp_sizeof_decimal(num))
    builtin.mp_encode_decimal(p, num)
end

local function encode_uuid(buf, uuid)
    local p = buf:alloc(builtin.mp_sizeof_uuid())
    builtin.mp_encode_uuid(p, uuid)
end

local function encode_int(buf, num)
    if num >= 0 then
        if num <= 0x7f then
            encode_fix(buf, 0, num)
        elseif num <= 0xff then
            encode_u8(buf, 0xcc, num)
        elseif num <= 0xffff then
            encode_u16(buf, 0xcd, num)
        elseif num <= 0xffffffff then
            encode_u32(buf, 0xce, num)
        else
            encode_u64(buf, 0xcf, 0ULL + num)
        end
    else
        if num >= -0x20 then
            encode_fix(buf, 0xe0, num)
        elseif num >= -0x80 then
            encode_u8(buf, 0xd0, num)
        elseif num >= -0x8000 then
            encode_u16(buf, 0xd1, num)
        elseif num >= -0x80000000 then
            encode_u32(buf, 0xd2, num)
        else
            encode_u64(buf, 0xd3, 0LL + num)
        end
    end
end

local function encode_str(buf, str)
    local len = #str
    buf:reserve(5 + len)
    if len <= 31 then
        encode_fix(buf, 0xa0, len)
    elseif len <= 0xff then
        encode_u8(buf, 0xd9, len)
    elseif len <= 0xffff then
        encode_u16(buf, 0xda, len)
    else
        encode_u32(buf, 0xdb, len)
    end
    local p = buf:alloc(len)
    ffi.copy(p, str, len)
end

local function encode_array(buf, size)
    if size <= 0xf then
        encode_fix(buf, 0x90, size)
    elseif size <= 0xffff then
        encode_u16(buf, 0xdc, size)
    else
        encode_u32(buf, 0xdd, size)
    end
end

local function encode_map(buf, size)
    if size <= 0xf then
        encode_fix(buf, 0x80, size)
    elseif size <= 0xffff then
        encode_u16(buf, 0xde, size)
    else
        encode_u32(buf, 0xdf, size)
    end
end

local function encode_bool(buf, val)
    encode_fix(buf, 0xc2, val and 1 or 0)
end

local function encode_bool_cdata(buf, val)
    encode_fix(buf, 0xc2, val ~= 0 and 1 or 0)
end

local function encode_nil(buf)
    local p = buf:alloc(1)
    p[0] = 0xc0
end

local function encode_r(buf, obj, level)
::restart::
    if type(obj) == "number" then
        -- Lua-way to check that number is an integer
        if obj % 1 == 0 and obj > -1e63 and obj < 1e64 then
            encode_int(buf, obj)
        else
            encode_double(buf, obj)
        end
    elseif type(obj) == "string" then
        encode_str(buf, obj)
    elseif type(obj) == "table" then
        if level >= msgpack.cfg.encode_max_depth then
            if not msgpack.cfg.encode_deep_as_nil then
                error(string.format('Too high nest level - %d',
                                    msgpack.cfg.encode_max_depth + 1))
            end
            encode_nil(buf)
            return
        end
        local serialize = nil
        local mt = getmetatable(obj)
        if mt ~= nil then
            serialize = mt.__serialize
        end
        -- calculate the number of array and map elements in the table
        -- TODO: pairs() aborts JIT
        local array_count, map_count = 0, 0
        for key in pairs(obj) do
            if type(key) == 'number' and key >= 1 and
               key == math.floor(key) and key == array_count + 1 then
                array_count = array_count + 1
            else
                map_count = map_count + 1
            end
        end
        if (serialize == nil and map_count == 0) or serialize == 'array' or
            serialize == 'seq' or serialize == 'sequence' then
            encode_array(buf, array_count)
            for i=1,array_count,1 do
                encode_r(buf, obj[i], level + 1)
            end
        elseif (serialize == nil and map_count > 0) or
            serialize == 'map' or serialize == 'mapping' then
            encode_map(buf, array_count + map_count)
            for key, val in pairs(obj) do
                encode_r(buf, key, level + 1)
                encode_r(buf, val, level + 1)
            end
        elseif type(serialize) == 'function' then
            obj = serialize(obj)
            goto restart
        else
            error("Invalid __serialize value")
        end
    elseif obj == nil then
        encode_nil(buf)
    elseif type(obj) == "boolean" then
        encode_bool(buf, obj)
    elseif type(obj) == "cdata" then
        if obj == nil then -- a workaround for nil
            encode_nil(buf, obj)
            return
        end
        local ctypeid = tonumber(ffi.typeof(obj))
        local fun = encode_ext_cdata[ctypeid]
        if fun ~= nil then
            fun(buf, obj)
        else
            error("can not encode FFI type: '"..ffi.typeof(obj).."'")
        end
    else
        error("can not encode Lua type: '"..type(obj).."'")
    end
end

local function encode(obj)
    local tmpbuf = cord_ibuf_take()
    encode_r(tmpbuf, obj, 0)
    local r = ffi.string(tmpbuf.rpos, tmpbuf:size())
    cord_ibuf_drop(tmpbuf)
    return r
end

on_encode(ffi.typeof('uint8_t'), encode_int)
on_encode(ffi.typeof('uint16_t'), encode_int)
on_encode(ffi.typeof('uint32_t'), encode_int)
on_encode(ffi.typeof('uint64_t'), encode_int)
on_encode(ffi.typeof('int8_t'), encode_int)
on_encode(ffi.typeof('int16_t'), encode_int)
on_encode(ffi.typeof('int32_t'), encode_int)
on_encode(ffi.typeof('int64_t'), encode_int)
on_encode(ffi.typeof('char'), encode_int)
on_encode(ffi.typeof('const char'), encode_int)
on_encode(ffi.typeof('unsigned char'), encode_int)
on_encode(ffi.typeof('const unsigned char'), encode_int)
on_encode(ffi.typeof('bool'), encode_bool_cdata)
on_encode(ffi.typeof('float'), encode_float)
on_encode(ffi.typeof('double'), encode_double)
on_encode(ffi.typeof('decimal_t'), encode_decimal)
on_encode(ffi.typeof('struct tt_uuid'), encode_uuid)

--------------------------------------------------------------------------------
-- Decoder
--------------------------------------------------------------------------------

local decode_r

-- See similar constants in utils.cc
local DBL_INT_MAX = 1e14 - 1

local function decode_u8(data)
    local num = ffi.cast(uint8_ptr_t, data[0])[0]
    data[0] = data[0] + 1
    return tonumber(num)
end

local decode_u16
if strict_alignment then
    decode_u16 = function(data)
        local u16 = ffi.new('uint16_t[1]')
        ffi.copy(u16, data[0], 2)
        data[0] = data[0] + 2
        return tonumber(bswap_u16(u16[0]))
    end
else
    decode_u16 = function(data)
        local num = bswap_u16(ffi.cast(uint16_ptr_t, data[0])[0])
        data[0] = data[0] + 2
        return tonumber(num)
    end
end

local decode_u32
if strict_alignment then
    decode_u32 = function(data)
        local u32 = ffi.new('uint32_t[1]')
        ffi.copy(u32, data[0], 4)
        data[0] = data[0] + 4
        return tonumber(
            ffi.cast('uint32_t', bit.bswap(tonumber(u32[0]))))
    end
else
    decode_u32 = function(data)
        local num = ffi.cast('uint32_t',
            bit.bswap(tonumber(ffi.cast(uint32_ptr_t, data[0])[0])))
        data[0] = data[0] + 4
        return tonumber(num)
    end
end

local decode_u64
if strict_alignment then
    decode_u64 = function(data)
        local u64 = ffi.new('uint64_t[1]')
        ffi.copy(u64, data[0], 8);
        data[0] = data[0] + 8
        local num = bit.bswap(u64[0])
        if num <= DBL_INT_MAX then
            return tonumber(num) -- return as 'number'
        end
        return num -- return as 'cdata'
    end
else
    decode_u64 = function(data)
        local num =
            bit.bswap(ffi.cast(uint64_ptr_t, data[0])[0])
        data[0] = data[0] + 8
        if num <= DBL_INT_MAX then
            return tonumber(num) -- return as 'number'
        end
        return num -- return as 'cdata'
    end
end

local function decode_i8(data)
    local num = ffi.cast(int8_ptr_t, data[0])[0]
    data[0] = data[0] + 1
    return tonumber(num)
end

local decode_i16
if strict_alignment then
    decode_i16 = function(data)
        local u16 = ffi.new('uint16_t[1]')
        ffi.copy(u16, data[0], 2)
        local num = bswap_u16(u16[0])
        data[0] = data[0] + 2
        -- note: this double cast is actually necessary
        return tonumber(ffi.cast('int16_t', ffi.cast('uint16_t', num)))
    end
else
    decode_i16 = function(data)
        local num = bswap_u16(ffi.cast(uint16_ptr_t, data[0])[0])
        data[0] = data[0] + 2
        -- note: this double cast is actually necessary
        return tonumber(ffi.cast('int16_t', ffi.cast('uint16_t', num)))
    end
end

local decode_i32
if strict_alignment then
    decode_i32 = function(data)
        local u32 = ffi.new('uint32_t[1]')
        ffi.copy(u32, data[0], 4)
        local num = bit.bswap(tonumber(u32[0]))
        data[0] = data[0] + 4
        return num
    end
else
    decode_i32 = function(data)
        local num = bit.bswap(tonumber(ffi.cast(uint32_ptr_t, data[0])[0]))
        data[0] = data[0] + 4
        return num
    end
end

local decode_i64
if strict_alignment then
    decode_i64 = function(data)
        local i64 = ffi.new('int64_t[1]')
        ffi.copy(i64, data[0], 8)
        data[0] = data[0] + 8
        local num = bit.bswap(i64[0])
        if num >= -DBL_INT_MAX and num <= DBL_INT_MAX then
            return tonumber(num) -- return as 'number'
        end
        return num -- return as 'cdata'
    end
else
    decode_i64 = function(data)
        local num = bit.bswap(ffi.cast('int64_t',
                ffi.cast(uint64_ptr_t, data[0])[0]))
        data[0] = data[0] + 8
        if num >= -DBL_INT_MAX and num <= DBL_INT_MAX then
            return tonumber(num) -- return as 'number'
        end
        return num -- return as 'cdata'
    end
end

local function decode_float(data)
    data[0] = data[0] - 1 -- mp_decode_float need type code
    return tonumber(builtin.mp_decode_float(data))
end

local function decode_double(data)
    data[0] = data[0] - 1 -- mp_decode_double need type code
    return tonumber(builtin.mp_decode_double(data))
end

local function decode_str(data, size)
    local ret = ffi.string(data[0], size)
    data[0] = data[0] + size
    return ret
end

local function decode_array(data, size)
    assert (type(size) == "number")
    local arr = {}
    for _ = 1, size do
        table.insert(arr, decode_r(data))
    end
    if not msgpack.cfg.decode_save_metatables then
        return arr
    end
    return setmetatable(arr, msgpack.array_mt)
end

local function decode_map(data, size)
    assert (type(size) == "number")
    local map = {}
    for _ = 1, size do
        local key = decode_r(data);
        local val = decode_r(data);
        map[key] = val
    end
    if not msgpack.cfg.decode_save_metatables then
        return map
    end
    return setmetatable(map, msgpack.map_mt)
end

local ext_decoder = {
    -- MP_UNKNOWN_EXTENSION
    [0] = function(data, len) error("unsupported extension type") end, -- luacheck: no unused args
    -- MP_DECIMAL
    [1] = function(data, len) local num = ffi.new("decimal_t") builtin.decimal_unpack(data, len, num) return num end,
    -- MP_UUID
    [2] = function(data, len)
        local uuid = ffi.new("struct tt_uuid")
        builtin.uuid_unpack(data, len, uuid)
        return uuid
    end,
}

local function decode_ext(data)
    local t = ffi.new("int8_t[1]")
    -- mp_decode_extl and mp_decode_decimal
    -- need type code
    data[0] = data[0] - 1
    local len = builtin.mp_decode_extl(data, t)
    local fun = ext_decoder[t[0]]
    if type(fun) == 'function' then
        return fun(data, len)
    else
        error("Unsupported extension type")
    end
end

local decoder_hint = {
    --[[{{{ MP_BIN]]
    [0xc4] = function(data) return decode_str(data, decode_u8(data)) end;
    [0xc5] = function(data) return decode_str(data, decode_u16(data)) end;
    [0xc6] = function(data) return decode_str(data, decode_u32(data)) end;

    --[[MP_FLOAT, MP_DOUBLE]]
    [0xca] = decode_float;
    [0xcb] = decode_double;

    --[[MP_UINT]]
    [0xcc] = decode_u8;
    [0xcd] = decode_u16;
    [0xce] = decode_u32;
    [0xcf] = decode_u64;

    --[[MP_INT]]
    [0xd0] = decode_i8;
    [0xd1] = decode_i16;
    [0xd2] = decode_i32;
    [0xd3] = decode_i64;

    --[[MP_STR]]
    [0xd9] = function(data) return decode_str(data, decode_u8(data)) end;
    [0xda] = function(data) return decode_str(data, decode_u16(data)) end;
    [0xdb] = function(data) return decode_str(data, decode_u32(data)) end;

    --[[MP_ARRAY]]
    [0xdc] = function(data) return decode_array(data, decode_u16(data)) end;
    [0xdd] = function(data) return decode_array(data, decode_u32(data)) end;

    --[[MP_MAP]]
    [0xde] = function(data) return decode_map(data, decode_u16(data)) end;
    [0xdf] = function(data) return decode_map(data, decode_u32(data)) end;
}

decode_r = function(data)
    local c = data[0][0]
    data[0] = data[0] + 1
    if c <= 0x7f then
        return tonumber(c) -- fixint
    elseif c >= 0xa0 and c <= 0xbf then
        return decode_str(data, bit.band(c, 0x1f)) -- fixstr
    elseif c >= 0x90 and c <= 0x9f then
        return decode_array(data, bit.band(c, 0xf)) -- fixarray
    elseif c >= 0x80 and c <= 0x8f then
        return decode_map(data, bit.band(c, 0xf)) -- fixmap
    elseif c >= 0xe0 then
        return tonumber(ffi.cast('signed char',c)) -- negfixint
    elseif c == 0xc0 then
        return msgpack.NULL
    elseif c == 0xc2 then
        return false
    elseif c == 0xc3 then
        return true
    elseif c >= 0xd4 and c <= 0xd8 or c >= 0xc7 and c <= 0xc9 then
        return decode_ext(data)
    else
        local fun = decoder_hint[c];
        assert (type(fun) == "function")
        return fun(data)
    end
end

---
-- All decode_XXX functions accept const char **data as its first argument,
-- like libmsgpuck does. After decoding data[0] position is changed to the next
-- element. It is significally faster on LuaJIT to use double pointer than
-- return result, newpos.
--

local function check_offset(offset, len)
    if offset == nil then
        return 1
    end
    local offset = ffi.cast('ptrdiff_t', offset)
    if offset < 1 or offset > len then
        error(string.format("offset = %d is out of bounds [1..%d]",
            tonumber(offset), len))
    end
    return offset
end

-- decode_unchecked(str, offset) -> res, new_offset
-- decode_unchecked(buf) -> res, new_buf
local function decode_unchecked(str, offset)
    if type(str) == "string" then
        offset = check_offset(offset, #str)
        local buf = ffi.cast(char_ptr_t, str)
        local bufp = ffi.new('const unsigned char *[1]', buf + offset - 1)
        local r = decode_r(bufp)
        return r, bufp[0] - buf + 1
    elseif ffi.istype(char_ptr_t, str) then
        -- Note: ffi.istype() ignores the const qualifier, so both
        -- (char *) and (const char *) buffers are valid.
        local bufp = ffi.new('const unsigned char *[1]', str)
        local r = decode_r(bufp)
        return r, ffi.cast(ffi.typeof(str), bufp[0])
    else
        error("msgpackffi.decode_unchecked(str, offset) -> res, new_offset | "..
              "msgpackffi.decode_unchecked([const] char *buf) -> res, new_buf")
    end
end

--------------------------------------------------------------------------------
-- exports
--------------------------------------------------------------------------------

return {
    NULL = msgpack.NULL;
    array_mt = msgpack.array_mt;
    map_mt = msgpack.map_mt;
    encode = encode;
    on_encode = on_encode;
    decode_unchecked = decode_unchecked;
    decode = decode_unchecked; -- just for tests
    internal = {
        encode_fix = encode_fix;
        encode_array = encode_array;
        encode_r = encode_r;
    }
}
