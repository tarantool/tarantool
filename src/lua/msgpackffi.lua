-- msgpackffi.lua (internal file)

local ffi = require('ffi')
local builtin = ffi.C

local MAXNESTING = 16
local NULL = ffi.cast('void *', 0)
local uint16_ptr_t = ffi.typeof('uint16_t *')
local uint32_ptr_t = ffi.typeof('uint32_t *')
local uint64_ptr_t = ffi.typeof('uint64_t *')
local float_ptr_t = ffi.typeof('float *')
local double_ptr_t = ffi.typeof('double *')
local const_char_ptr_t = ffi.typeof('const char *')

ffi.cdef([[
uint32_t bswap_u32(uint32_t x);
uint64_t bswap_u64(uint64_t x);
double
mp_bswap_double(double d);
float
mp_bswap_float(float f);
]])
local function bswap_u16(num)
    return bit.rshift(bit.bswap(tonumber(num)), 16)
end
local bswap_u32 = builtin.bswap_u32
local bswap_u64 = builtin.bswap_u64
--[[ -- LuaJIT 2.1
local bswap_u32 = bit.bswap
local bswap_u64 = bit.bswap
--]]

--------------------------------------------------------------------------------
--- Buffer
--------------------------------------------------------------------------------

local DEFAULT_CAPACITY = 4096;

local tmpbuf = {}
tmpbuf.s = ffi.new("char[?]", DEFAULT_CAPACITY)
tmpbuf.e = tmpbuf.s + DEFAULT_CAPACITY
tmpbuf.p = tmpbuf.s
tmpbuf.reserve = function(buf, needed)
    if buf.p + needed <= buf.e then
        return
    end

    local size = buf.p - buf.s
    local capacity = buf.e - buf.s
    while capacity - size < needed do
        capacity = 2 * capacity
    end

    local s = ffi.new("char[?]", capacity)
    ffi.copy(s, buf.s, size)
    buf.s = s
    buf.e = s + capacity
    buf.p = s + size
end
tmpbuf.reset = function(buf)
    buf.p = buf.s
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
    buf:reserve(1)
    buf.p[0] = bit.bor(code, tonumber(num))
    -- buf.p[0] = bit.bor(code, num) -- LuaJIT 2.1
    buf.p = buf.p + 1
end

local function encode_u8(buf, code, num)
    buf:reserve(2)
    buf.p[0] = code
    ffi.cast(uint16_ptr_t, buf.p + 1)[0] = num
    buf.p = buf.p + 2
end

local function encode_u16(buf, code, num)
    buf:reserve(3)
    buf.p[0] = code
    ffi.cast(uint16_ptr_t, buf.p + 1)[0] = bswap_u16(num)
    buf.p = buf.p + 3
end

local function encode_u32(buf, code, num)
    buf:reserve(5)
    buf.p[0] = code
    ffi.cast(uint32_ptr_t, buf.p + 1)[0] = bswap_u32(num)
    buf.p = buf.p + 5
end

local function encode_u64(buf, code, num)
    buf:reserve(9)
    buf.p[0] = code
    ffi.cast(uint64_ptr_t, buf.p + 1)[0] = bswap_u64(num)
    buf.p = buf.p + 9
end

local function encode_float(buf, num)
    buf:reserve(5)
    buf.p[0] = 0xca;
    ffi.cast(float_ptr_t, buf.p + 1)[0] = builtin.mp_bswap_float(num)
    buf.p = buf.p + 5
end

local function encode_double(buf, num)
    buf:reserve(9)
    buf.p[0] = 0xcb;
    ffi.cast(double_ptr_t, buf.p + 1)[0] = builtin.mp_bswap_double(num)
    buf.p = buf.p + 9
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
        elseif num <= 18446744073709551615ULL then
            encode_u64(buf, 0xcf, num)
        else
            encode_double(buf, num)
        end
    else
        if num >= -0x20 then
            encode_fix(buf, 0xe0, num)
        elseif num >= -0x7f then
            encode_u8(buf, 0xd0, num)
        elseif num >= -0x7fff then
            encode_u16(buf, 0xd1, num)
        elseif num >= -0x7fffffff then
            encode_u32(buf, 0xd2, num)
        elseif num >= -9223372036854775807LL then
            encode_u64(buf, 0xd3, num)
        else
            encode_double(buf, num)
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
    ffi.copy(buf.p, str, len)
    buf.p = buf.p + len
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
    buf:reserve(1)
    buf.p[0] = 0xc0
    buf.p = buf.p + 1
end

local function encode_r(buf, obj, level)
    if type(obj) == "number" then
        if obj % 1 == 0 then -- Lua-way to check that number is an integer
            encode_int(buf, obj)
        else
            encode_double(buf, obj)
        end
    elseif type(obj) == "string" then
        encode_str(buf, obj)
    elseif type(obj) == "table" then
        if level >= MAXNESTING then -- Limit nested tables
            encode_nil(buf)
            return
        end
        if #obj > 0 then
            encode_array(buf, #obj)
            local i
            for i=1,#obj,1 do
                encode_r(buf, obj[i], level + 1)
            end
        else
            local size = 0
            local key, val
            for key, val in pairs(obj) do -- goodbye, JIT
                size = size + 1
            end
            encode_map(buf, size)
            for key, val in pairs(obj) do
                encode_r(buf, key, level + 1)
                encode_r(buf, val, level + 1)
            end
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
    tmpbuf:reset()
    encode_r(tmpbuf, obj, 0)
    return ffi.string(tmpbuf.s, tmpbuf.p - tmpbuf.s)
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

--------------------------------------------------------------------------------
-- Decoder
--------------------------------------------------------------------------------

local decode_r

local function decode_u8(data)
    local num = ffi.cast('uint8_t *', data[0])[0]
    data[0] = data[0] + 1
    return num
end

local function decode_u16(data)
    local num = bswap_u16(ffi.cast(uint16_ptr_t, data[0])[0])
    data[0] = data[0] + 2
    return num
end

local function decode_u32(data)
    local num = bswap_u32(ffi.cast(uint32_ptr_t, data[0])[0])
    data[0] = data[0] + 4
    return num
end

local function decode_u64(data)
    local num = bswap_u64(ffi.cast(uint64_ptr_t, data[0])[0])
    data[0] = data[0] + 8
    return num
end

local function decode_i8(data)
    local num = ffi.cast(uint8_ptr_t, data[0])[0]
    data[0] = data[0] + 1
    return num
end

local function decode_i16(data)
    local num = bswap_u16(ffi.cast(uint16_ptr_t, data[0])[0])
    data[0] = data[0] + 2
     return tonumber(ffi.cast('int16_t', ffi.cast('uint16_t', num)))
end

local function decode_i32(data)
    local num = bswap_u32(ffi.cast(uint32_ptr_t, data[0])[0])
    data[0] = data[0] + 4
    return tonumber(ffi.cast('int32_t', ffi.cast('uint32_t', num)))
end

local function decode_i64(data)
    local num = bswap_u64(ffi.cast(uint64_ptr_t, data[0])[0])
    data[0] = data[0] + 8
    return ffi.cast('int64_t', ffi.cast('uint64_t', num))
end

local function decode_float(data)
    local num = builtin.mp_bswap_float(ffi.cast(float_ptr_t,data[0])[0])
    data[0] = data[0] + 4
    return tonumber(num)
end

local function decode_double(data)
    local num = builtin.mp_bswap_double(ffi.cast(double_ptr_t, data[0])[0])
    data[0] = data[0] + 8
    return tonumber(num)
end

local function decode_str(data, size)
    local ret = ffi.string(data[0], size)
    data[0] = data[0] + size
    return ret
end

local function decode_array(data, size)
    assert (type(size) == "number")
    local arr = {}
    local i
    for i=1,size,1 do
        table.insert(arr, decode_r(data))
    end
    return arr
end

local function decode_map(data, size)
    assert (type(size) == "number")
    local map = {}
    local i
    for i=1,size,1 do
        local key = decode_r(data);
        local val = decode_r(data);
        map[key] = val
    end
    return map
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
        return c -- fixint
    elseif c >= 0xa0 and c <= 0xbf then
        return decode_str(data, bit.band(c, 0x1f)) -- fixstr
    elseif c >= 0x90 and c <= 0x9f then
        return decode_array(data, bit.band(c, 0xf)) -- fixarray
    elseif c >= 0x80 and c <= 0x8f then
        return decode_map(data, bit.band(c, 0xf)) -- fixmap
    elseif c <= 0x7f then
        return c2
    elseif c >= 0xe0 then
        return ffi.cast('signed char',c)
    elseif c == 0xc0 then
        return NULL
    elseif c == 0xc2 then
        return false
    elseif c == 0xc3 then
        return true
    else
        local fun = decoder_hint[c];
        assert (type(fun) == "function")
        return fun(data)
    end
end

---
-- A temporary const char ** buffer.
-- All decode_XXX functions accept const char **data as its first argument,
-- like libmsgpuck does. After decoding data[0] position is changed to the next
-- element. It is significally faster on LuaJIT to use double pointer than
-- return result, newpos.
--
local bufp = ffi.new('const unsigned char *[1]');

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
        local buf = ffi.cast(const_char_ptr_t, str)
        bufp[0] = buf + offset - 1
        local r = decode_r(bufp)
        return r, bufp[0] - buf + 1
    elseif ffi.istype(const_char_ptr_t, str) then
        bufp[0] = str
        local r = decode_r(bufp)
        return r, bufp[0]
    else
        error("msgpackffi.decode_unchecked(str, offset) -> res, new_offset | "..
              "msgpackffi.decode_unchecked(const char *buf) -> res, new_buf")
    end
end

--------------------------------------------------------------------------------
-- box-specific optimized API
--------------------------------------------------------------------------------

local function encode_tuple(obj)
    tmpbuf:reset()
    if obj == nil then
        encode_fix(tmpbuf, 0x90, 0)  -- empty array
    elseif type(obj) == "table" then
        encode_array(tmpbuf, #obj)
        local i
        for i=1,#obj,1 do
            encode_r(tmpbuf, obj[i], 1)
        end
    else
        encode_fix(tmpbuf, 0x90, 1)  -- array of one element
        encode_r(tmpbuf, obj, 1)
    end
    return tmpbuf.s, tmpbuf.p
end

--------------------------------------------------------------------------------
-- exports
--------------------------------------------------------------------------------

return {
    NULL = NULL;
    encode = encode;
    on_encode = on_encode;
    decode_unchecked = decode_unchecked;
    encode_tuple = encode_tuple;
}
