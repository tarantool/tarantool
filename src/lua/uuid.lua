-- uuid.lua (internal file)

---@alias uuid.byte_order
---| "l" # little-endian
---| "b" # big-endian
---| "h" # host (endianness depends on host)
---| "n" # network (endianness depends on network)
---| "host" # host (endianness depends on host)
---| "network" # network (endianness depends on network)

---A UUID object. The `__tostring` metamethod converts it to a 36-byte
---hexadecimal string, so `tostring(u)` works; `u:str()` returns the
---same string.
---@class uuid: ffi.cdata*
---@field bin fun(self: uuid, byte_order?: uuid.byte_order): string Return
---    the UUID as a 16-byte binary string.
---@field str fun(self: uuid): string Return the UUID as a 36-byte
---    hexadecimal string.
---@field isnil fun(self: uuid): boolean Check if the UUID is an all-zero
---    value.
---@operator eq(uuid|string): boolean
---@operator lt(uuid|string): boolean
---@operator le(uuid|string): boolean

---@class uuid_module
---@operator call: uuid
---@field NULL uuid A nil UUID object (all zero bytes).
---@field new fun(): uuid Create a UUID.
---@field fromstr fun(str: string): uuid? Create a UUID from a 36-byte
---    hexadecimal string.
---@field frombin fun(bin: string, byte_order?: uuid.byte_order): uuid Create
---    a UUID from a 16-byte binary string.
---@field bin fun(byte_order?: uuid.byte_order): string Create a UUID and
---    return its 16-byte binary form.
---@field str fun(): string Create a UUID and return its 36-byte
---    hexadecimal form.
---@field is_uuid fun(value: any): boolean Check if the specified value
---    has the UUID type.

local ffi = require("ffi")
local buffer = require('buffer')
local builtin = ffi.C

ffi.cdef[[
void
tt_uuid_create(struct tt_uuid *uu);
int
tt_uuid_from_string(const char *in, struct tt_uuid *uu);
void
tt_uuid_to_string(const struct tt_uuid *uu, char *out);
void
tt_uuid_bswap(struct tt_uuid *uu);
bool
tt_uuid_is_nil(const struct tt_uuid *uu);
bool
tt_uuid_is_equal(const struct tt_uuid *lhs, const struct tt_uuid *rhs);
int
tt_uuid_compare(const struct tt_uuid *a, const struct tt_uuid *b);
extern const struct tt_uuid uuid_nil;
]]

local uuid_t = ffi.typeof('struct tt_uuid')
local UUID_STR_LEN = 36
local UUID_LEN = ffi.sizeof(uuid_t)
local uuid_stash = buffer.ffi_stash_new(uuid_t)
local uuid_stash_take = uuid_stash.take
local uuid_stash_put = uuid_stash.put

local uuid_str_stash =
    buffer.ffi_stash_new(string.format('char[%s]', UUID_STR_LEN + 1))
local uuid_str_stash_take = uuid_str_stash.take
local uuid_str_stash_put = uuid_str_stash.put

---# Builtin `uuid` module
---
---A "UUID" is a [Universally unique identifier](https://en.wikipedia.org/wiki/Universally_unique_identifier).
---
---If an application requires that a value be unique only within a single
---computer or on a single database, then a simple counter is better than a
---UUID, because getting a UUID is time-consuming (it requires a
---[syscall](https://en.wikipedia.org/wiki/Syscall)). For clusters of
---computers, or widely distributed applications, UUIDs are better.
---
---Tarantool generates UUIDs following the rules for RFC 4122
---[version 4 variant 1](https://en.wikipedia.org/wiki/Universally_unique_identifier#Version_4_(random)).
---
---**Example:**
---
---```tarantoolsession
---tarantool> uuid = require('uuid')
---    ---
---    ...
---
---tarantool> uuid(), uuid.bin(), uuid.str()
---    ---
---    - 16ffedc8-cbae-4f93-a05e-349f3ab70baa
---    - !!binary FvG+Vy1MfUC6kIyeM81DYw==
---    - 67c999d2-5dce-4e58-be16-ac1bcb93160f
---    ...
---```
---
---**Example:**
---
---```tarantoolsession
---tarantool> uu = uuid()
---    ---
---    ...
---
---tarantool> #uu:bin(), #uu:str(), type(uu), uu:isnil()
---    ---
---    - 16
---    - 36
---    - cdata
---    - false
---    ...
---```
local uuid = {}
---@cast uuid uuid_module

---Check if the specified value has the UUID type.
---
---*Since 2.6.1*
---
---@param value any a value to check.
---@return boolean is_uuid true if the specified value is a uuid, and false
---    otherwise.
function uuid.is_uuid(value)
    return ffi.istype(uuid_t, value)
end

---Convert UUID to a 36-byte hexadecimal string.
---
---@param uu uuid
---@return string uuid 36-byte hexadecimal string
local uuid_tostring = function(uu)
    if not uuid.is_uuid(uu) then
        error('Usage: uuid:str()', 0)
    end
    local strbuf = uuid_str_stash_take()
    builtin.tt_uuid_to_string(uu, strbuf)
    uu = ffi.string(strbuf, UUID_STR_LEN)
    uuid_str_stash_put(strbuf)
    return uu
end

---Convert a hexadecimal 36-byte string to a UUID object.
---
---@param str string UUID in 36-byte hexadecimal string.
---@return uuid? uu converted UUID, or nil if the string is invalid.
function uuid.fromstr(str)
    if type(str) ~= 'string' then
        error("fromstr(str)")
    end
    local uu = ffi.new(uuid_t)
    local rc = builtin.tt_uuid_from_string(str, uu)
    if rc ~= 0 then
        return nil
    end
    return uu
end

local need_bswap = function(order)
    if order == nil or order == 'l' or order == 'h' or order == 'host' then
        return false
    elseif order == 'b' or order == 'n' or order == 'network' then
        return true
    else
        error('invalid byteorder, valid is l, b, h, n')
    end
end

---Convert UUID to a 16-byte string.
---
---@param uu uuid
---@param byteorder? uuid.byte_order byte order of the resulting UUID.
---@return string uuid 16-byte string
local uuid_tobin = function(uu, byteorder)
    if not uuid.is_uuid(uu) then
        error('Usage: uuid:bin([byteorder])', 0)
    end
    if need_bswap(byteorder) then
        local uuidbuf = uuid_stash_take()
        ffi.copy(uuidbuf, uu, UUID_LEN)
        builtin.tt_uuid_bswap(uuidbuf)
        uu = ffi.string(ffi.cast('char *', uuidbuf), UUID_LEN)
        uuid_stash_put(uuidbuf)
        return uu
    end
    return ffi.string(ffi.cast('char *', uu), UUID_LEN)
end

---Convert a binary 16-byte string to a UUID object.
---
---@param bin string UUID in 16-byte binary string.
---@param byteorder? uuid.byte_order byte order of the given string.
---@return uuid uu converted UUID.
function uuid.frombin(bin, byteorder)
    if type(bin) ~= 'string' or #bin ~= UUID_LEN then
        error("frombin(bin, [byteorder])")
    end
    local uu = ffi.new(uuid_t)
    ffi.copy(uu, bin, UUID_LEN)
    if need_bswap(byteorder) then
        builtin.tt_uuid_bswap(uu)
    end
    return uu
end

---Check if the UUID is an all-zero value.
---
---The all-zero UUID value can be expressed as `uuid.NULL`, or as
---`uuid.fromstr('00000000-0000-0000-0000-000000000000')`. The comparison
---with an all-zero value can also be expressed as
---`uuid_with_type_cdata == uuid.NULL`.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> uu = uuid()
--- ---
--- ...
--- tarantool> uu:isnil()
--- ---
--- - false
--- ...
--- ```
---
---@param uu uuid
---@return boolean is_nil true if the value is all zero, otherwise false.
local uuid_isnil = function(uu)
    if not uuid.is_uuid(uu) then
        error('Usage: uuid:isnil()', 0)
    end
    return builtin.tt_uuid_is_nil(uu)
end

local function error_convert_arg(arg_index)
    local err_fmt = 'incorrect value to convert to uuid as %d argument'
    error(err_fmt:format(arg_index), 3)
end

local uuid_eq = function(lhs, rhs)
    if not uuid.is_uuid(lhs) then
        lhs, rhs = rhs, lhs
    elseif uuid.is_uuid(rhs) then
        return builtin.tt_uuid_is_equal(lhs, rhs)
    end
    if type(rhs) ~= 'string' then
        return false
    end
    local buf = uuid_stash_take()
    local rc = builtin.tt_uuid_from_string(rhs, buf) == 0
    if rc then
        rc = builtin.tt_uuid_is_equal(lhs, buf)
    else
        rc = false
    end
    uuid_stash_put(buf)
    return rc
end

---Create a UUID.
---
---You can use it in an index over a UUID field.
---
---*Since 2.4.1*
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> box.space.test:create_index("pk",
---          >   {parts={{field = 1, type = 'uuid'}}})
--- ---
--- ...
--- tarantool> box.space.test:insert{uuid.new()}
--- ---
--- - [e631fdcc-0e8a-4d2f-83fd-b0ce6762b13f]
--- ...
--- ```
---
---@return uuid uu a UUID.
function uuid.new()
    local uu = ffi.new(uuid_t)
    builtin.tt_uuid_create(uu)
    return uu
end

---Get UUID as a 16-byte string. An optimized shortcut for `new():bin()`.
---
---@param byteorder? uuid.byte_order byte order of the resulting UUID.
---@return string uuid 16-byte string
function uuid.bin(byteorder)
    local uuidbuf = uuid_stash_take()
    builtin.tt_uuid_create(uuidbuf)
    local res = uuid_tobin(uuidbuf, byteorder)
    uuid_stash_put(uuidbuf)
    return res
end
---Get UUID as a 36-byte hexadecimal string. An optimized shortcut for
---`new():str()`.
---
---@return string uuid 36-byte hexadecimal string
function uuid.str()
    local uuidbuf = uuid_stash_take()
    builtin.tt_uuid_create(uuidbuf)
    local res = uuid_tostring(uuidbuf)
    uuid_stash_put(uuidbuf)
    return res
end

local uuid_cmp = function(lhs, rhs)
    local buf
    if uuid.is_uuid(lhs) then
        if uuid.is_uuid(rhs) then
            -- Fast path.
           return builtin.tt_uuid_compare(lhs, rhs)
        end
        if type(rhs) ~= 'string' then
            return error_convert_arg(2)
        end
        buf = uuid_stash_take()
        if builtin.tt_uuid_from_string(rhs, buf) ~= 0 then
            uuid_stash_put(buf)
            return error_convert_arg(2)
        end
        rhs = buf
    else
        if type(lhs) ~= 'string' then
            return error_convert_arg(1)
        end
        buf = uuid_stash_take()
        if builtin.tt_uuid_from_string(lhs, buf) ~= 0 then
            uuid_stash_put(buf)
            return error_convert_arg(1)
        end
        lhs = buf
    end
    local rc = builtin.tt_uuid_compare(lhs, rhs)
    uuid_stash_put(buf)
    return rc
end
local uuid_lt = function(lhs, rhs)
    return uuid_cmp(lhs, rhs) < 0
end
local uuid_le = function(lhs, rhs)
    return uuid_cmp(lhs, rhs) <= 0
end

local uuid_mt = {
    __tostring = uuid_tostring;
    __eq = uuid_eq;
    __lt = uuid_lt;
    __le = uuid_le;
    __index = {
        isnil = uuid_isnil;
        bin   = uuid_tobin;    -- binary host byteorder
        str   = uuid_tostring; -- RFC4122 string
    }
}

ffi.metatype(uuid_t, uuid_mt)

---A nil UUID object (all zero bytes).
---@type uuid
uuid.NULL = builtin.uuid_nil

setmetatable(uuid, {
    __call = uuid.new, -- shortcut for new()
})

return uuid
