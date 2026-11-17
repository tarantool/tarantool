-- ulid.lua (internal file)

---*Since 3.6.0*
---
---The `ulid` module implements ULID (Universally Unique Lexicographically
---Sortable Identifier) support in Tarantool. A ULID is a 128-bit identifier
---consisting of:
---
---* a 48-bit timestamp in milliseconds since the Unix epoch
---* an 80-bit random (entropy) component
---
---ULID strings are encoded using **Crockford Base32**, a compact and
---human-friendly alphabet that excludes visually ambiguous characters.
---
---In binary form, ULIDs are represented as 16-byte values in **big-endian**
---byte order. This ensures that the lexicographical order of binary ULIDs
---matches their chronological order, in accordance with the ULID
---specification.
---
---ULIDs have several useful properties:
---
---* They are lexicographically sortable by creation time.
---* They fit entirely into 26 ASCII characters.
---* They avoid visually ambiguous symbols (`I`, `L`, `O`, `U`).
---* They have 128 bits of total uniqueness-same as UUID v4.
---
---Tarantool uses a *monotonic* ULID generator. This ensures that multiple
---ULIDs created within the same millisecond are strictly increasing and
---preserve sort order: for any two ULIDs generated in the same millisecond,
---the one created later is greater than the earlier one.
---
---Internally, the monotonic generator keeps the last generated ULID for the
---current millisecond and increments the 80-bit random part for each
---subsequent ULID. A real overflow of the random part can happen only after
---generating `2^80` ULIDs within the same millisecond, which is practically
---impossible on real hardware. However, for strict correctness of the ULID
---specification and to avoid silent wrap-around, the implementation detects
---this overflow and fails the next generation attempt with a Lua error
---(`ULID random component overflow`).
---
---To use this module, run the following command:
---
---```lua
---local ulid = require('ulid')
---```
---
---**Comparison**
---
---ULID objects support the full set of Lua comparison operators:
---
---* `==` and `~=` - equality and inequality.
---* `<` and `<=` - lexicographical comparison.
---* `>` and `>=` - lexicographical comparison.
---
---The comparison is based on the internal 16-byte representation in
---big-endian order and is consistent with the ULID specification: for ULIDs
---created by the monotonic generator, later ULIDs are greater than earlier
---ones, including ULIDs generated within the same millisecond.
---
---Comparison works both between ULID objects and between a ULID object and a
---ULID string:
---
---* `u1 == u2` compares two ULID objects directly.
---* `u1 == "01..."` converts the string to ULID and compares values.
---* `u1 < "01..."` or `"01..." < u1` convert the string argument to ULID and
---  perform lexicographical comparison.
---
---**Examples**
---
---Basic usage:
---
---```tarantoolsession
---tarantool> u_obj = ulid.new()
---tarantool> u_str = ulid.str()
---tarantool> u_bin = ulid.bin()
---
---tarantool> u_obj, u_str, u_bin
---    ---
---    - 06DGE6SEZDCFFSFEAJFMC9YQAR
---    - 06DGE6T0DW0N2N6KMPVZ8SGE4W
---    - "\x01\x9B\a\eSz8\xBA\xB3\xC5\xCC\xFE\x18\xBB1\xD6"
---    ...
---```
---
---Creating a ULID object and inspecting it:
---
---```tarantoolsession
---tarantool> u = ulid()
---    ---
---    ...
---
---tarantool> #u:bin(), #u:str(), type(u), u:isnil()
---    ---
---    - 16
---    - 26
---    - cdata
---    - false
---    ...
---
---tarantool> tostring(u) == u:str()
---    ---
---    - true
---    ...
---```
---
---Converting between string and binary formats:
---
---```tarantoolsession
---tarantool> s = ulid.str()
---tarantool> s
---    ---
---    - 06DGE70CPFDV344XX43687N1SM
---    ...
---
---tarantool> u = ulid.fromstr(s)
---tarantool> u:str() == s
---    ---
---    - true
---    ...
---
---tarantool> b = u:bin()
---tarantool> u2 = ulid.frombin(b)
---tarantool> u2 == u
---    ---
---    - true
---    ...
---```
---
---Working with `ulid.NULL`:
---
---```tarantoolsession
---tarantool> ulid.NULL
---    ---
---    - 00000000000000000000000000
---    ...
---
---tarantool> ulid.NULL:isnil()
---    ---
---    - true
---    ...
---
---tarantool> ulid.new():isnil()
---    ---
---    - false
---    ...
---```
---
---Comparison operators:
---
---```tarantoolsession
---tarantool> u1 = ulid.new()
---tarantool> u2 = ulid.new()
---tarantool> u1 < u2
---    ---
---    - true
---    ...
---```
---
---Checking types:
---
---```tarantoolsession
---tarantool> u = ulid.new()
---tarantool> ulid.is_ulid(u)
---    ---
---    - true
---    ...
---
---tarantool> ulid.is_ulid("06DGE7RJK8QWJE27X5VVCC5VDW")
---    ---
---    - false
---    ...
---```
---
---Generating many ULIDs:
---
---```tarantoolsession
---tarantool> for i = 1,5 do print(ulid.str()) end
---    ---
---    06DGE7T8AJD6EDNJ3VQ2ZYB3R4
---    06DGE7T8AJD6EDNJ3VQ2ZYB3R8
---    06DGE7T8AJD6EDNJ3VQ2ZYB3RC
---    06DGE7T8AJD6EDNJ3VQ2ZYB3RG
---    06DGE7T8AJD6EDNJ3VQ2ZYB3RM
---    ...
---```
local ulid = {}

---A ULID object returned by `ulid.new()`, `ulid.fromstr()`, or
---`ulid.frombin()`.
---@class ulid: ffi.cdata*
---@field bin fun(self: ulid): string Return the ULID as a 16-byte
---    binary string.
---@field str fun(self: ulid): string Return the ULID as a 26-character
---    string.
---@field isnil fun(self: ulid): boolean Check if the ULID is the nil
---    ULID (all 16 bytes are zero).
---@field __tostring fun(self: ulid): string Return the ULID in string
---    form.
---@operator eq(ulid|string): boolean
---@operator lt(ulid|string): boolean
---@operator le(ulid|string): boolean

local ffi = require('ffi')
local buffer = require('buffer')
local builtin = ffi.C

ffi.cdef[[
struct tt_ulid {
    unsigned char bytes[16];
};
int
tt_ulid_create(struct tt_ulid *u);
int
tt_ulid_from_string(const char *in, struct tt_ulid *u);
void
tt_ulid_to_string(const struct tt_ulid *u, char *out);
bool
tt_ulid_is_nil(const struct tt_ulid *u);
bool
tt_ulid_is_equal(const struct tt_ulid *lhs, const struct tt_ulid *rhs);
int
tt_ulid_compare(const struct tt_ulid *lhs, const struct tt_ulid *rhs);
extern const struct tt_ulid ulid_nil;
]]

local ulid_t = ffi.typeof('struct tt_ulid')
local ULID_STR_LEN = 26
local ULID_LEN = ffi.sizeof(ulid_t)

local ulid_stash = buffer.ffi_stash_new(ulid_t)
local ulid_stash_take = ulid_stash.take
local ulid_stash_put = ulid_stash.put

local ulid_str_stash =
    buffer.ffi_stash_new(string.format('char[%s]', ULID_STR_LEN + 1))
local ulid_str_stash_take = ulid_str_stash.take
local ulid_str_stash_put = ulid_str_stash.put

---Check if the given value is a ULID cdata object.
---
---@param value any a value of any type.
---@return boolean result true if the value is a ULID, otherwise false.
function ulid.is_ulid(value)
    return ffi.istype(ulid_t, value)
end

---Return the ULID as a 26-character string.
---
---ULID objects also implement the standard Lua `__tostring` metamethod. This
---means that calling `tostring(u)` for a ULID object `u` returns the same
---value as `u:str()`.
---
---@param u ulid
---@return string result the ULID in string form.
local ulid_tostring = function(u)
    if not ulid.is_ulid(u) then
        error('Usage: ulid:str()', 0)
    end
    local strbuf = ulid_str_stash_take()
    builtin.tt_ulid_to_string(u, strbuf)
    local res = ffi.string(strbuf, ULID_STR_LEN)
    ulid_str_stash_put(strbuf)
    return res
end

---Create a ULID object from a 26-character string.
---
---The input must be a valid ULID string encoded using Crockford Base32. If
---the string is invalid (wrong length or invalid symbols), nil is returned.
---
---@param str string ULID in 26-character string form.
---@return ulid? u converted ULID, or nil if the string is invalid.
function ulid.fromstr(str)
    if type(str) ~= 'string' then
        error('fromstr(str)')
    end
    local u = ffi.new(ulid_t)
    local rc = builtin.tt_ulid_from_string(str, u)
    if rc ~= 0 then
        return nil
    end
    return u
end

---Return the ULID as a 16-byte binary string.
---
---@param u ulid
---@return string result the ULID in binary form.
local ulid_tobin = function(u)
    if not ulid.is_ulid(u) then
        error('Usage: ulid:bin()', 0)
    end
    return ffi.string(ffi.cast('char *', u), ULID_LEN)
end

---Create a ULID object from a 16-byte binary string.
---
---@param bin string ULID in 16-byte binary string form.
---@return ulid u converted ULID.
function ulid.frombin(bin)
    if type(bin) ~= 'string' or #bin ~= ULID_LEN then
        error('frombin(bin)')
    end
    local u = ffi.new(ulid_t)
    ffi.copy(u, bin, ULID_LEN)
    return u
end

---Check if the ULID is the nil ULID (all 16 bytes are zero).
---
---@param u ulid
---@return boolean result true for `ulid.NULL`, otherwise false.
local ulid_isnil = function(u)
    if not ulid.is_ulid(u) then
        error('Usage: ulid:isnil()', 0)
    end
    return builtin.tt_ulid_is_nil(u)
end

local function error_convert_arg(arg_index)
    local err_fmt = 'incorrect value to convert to ulid as %d argument'
    error(err_fmt:format(arg_index), 3)
end

local ulid_eq = function(lhs, rhs)
    if not ulid.is_ulid(lhs) then
        lhs, rhs = rhs, lhs
    elseif ulid.is_ulid(rhs) then
        return builtin.tt_ulid_is_equal(lhs, rhs)
    end
    if type(rhs) ~= 'string' then
        return false
    end
    local buf = ulid_stash_take()
    local ok = builtin.tt_ulid_from_string(rhs, buf) == 0
    local rc
    if ok then
        rc = builtin.tt_ulid_is_equal(lhs, buf)
    else
        rc = false
    end
    ulid_stash_put(buf)
    return rc
end

local function ulid_create_checked(u)
    local rc = builtin.tt_ulid_create(u)
    if rc ~= 0 then
        error('ULID random component overflow', 2)
    end
end

---Create a new ULID object.
---
---This function uses the monotonic generator. Multiple ULIDs created within
---the same millisecond are strictly increasing.
---
---@return ulid u a new ULID object.
function ulid.new()
    local u = ffi.new(ulid_t)
    ulid_create_checked(u)
    return u
end

---Create a new ULID and return its binary representation as a 16-byte
---string. A shortcut for `new():bin()`.
---
---@return string result the ULID in binary form.
function ulid.bin()
    local ulidbuf = ulid_stash_take()
    ulid_create_checked(ulidbuf)
    local res = ulid_tobin(ulidbuf)
    ulid_stash_put(ulidbuf)
    return res
end

---Create a new ULID and return its string representation. The result is
---always 26 characters, encoded using Crockford Base32. A shortcut for
---`new():str()`.
---
---@return string result a ULID string.
function ulid.str()
    local ulidbuf = ulid_stash_take()
    ulid_create_checked(ulidbuf)
    local res = ulid_tostring(ulidbuf)
    ulid_stash_put(ulidbuf)
    return res
end

local ulid_cmp = function(lhs, rhs)
    local buf
    if ulid.is_ulid(lhs) then
        if ulid.is_ulid(rhs) then
            return builtin.tt_ulid_compare(lhs, rhs)
        end
        if type(rhs) ~= 'string' then
            return error_convert_arg(2)
        end
        buf = ulid_stash_take()
        if builtin.tt_ulid_from_string(rhs, buf) ~= 0 then
            ulid_stash_put(buf)
            return error_convert_arg(2)
        end
        rhs = buf
    else
        if type(lhs) ~= 'string' then
            return error_convert_arg(1)
        end
        buf = ulid_stash_take()
        if builtin.tt_ulid_from_string(lhs, buf) ~= 0 then
            ulid_stash_put(buf)
            return error_convert_arg(1)
        end
        lhs = buf
    end
    local rc = builtin.tt_ulid_compare(lhs, rhs)
    ulid_stash_put(buf)
    return rc
end

local ulid_lt = function(lhs, rhs)
    return ulid_cmp(lhs, rhs) < 0
end

local ulid_le = function(lhs, rhs)
    return ulid_cmp(lhs, rhs) <= 0
end

local ulid_mt = {
    __tostring = ulid_tostring;
    __eq = ulid_eq;
    __lt = ulid_lt;
    __le = ulid_le;
    __index = {
        isnil = ulid_isnil;
        bin   = ulid_tobin;
        str   = ulid_tostring;
    }
}

ffi.metatype(ulid_t, ulid_mt)

---A nil ULID object (all 16 bytes are zero).
---@type ulid
ulid.NULL = builtin.ulid_nil

setmetatable(ulid, {
    __call = ulid.new; -- shortcut for new()
})

return ulid
