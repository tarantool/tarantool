-- ulid.lua (internal file)

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

local is_ulid = function(value)
    return ffi.istype(ulid_t, value)
end

local ulid_tostring = function(u)
    if not is_ulid(u) then
        return error('Usage: ulid:str()')
    end
    local strbuf = ulid_str_stash_take()
    builtin.tt_ulid_to_string(u, strbuf)
    local res = ffi.string(strbuf, ULID_STR_LEN)
    ulid_str_stash_put(strbuf)
    return res
end

local ulid_fromstr = function(str)
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

local ulid_tobin = function(u)
    if not is_ulid(u) then
        return error('Usage: ulid:bin()')
    end
    return ffi.string(ffi.cast('char *', u), ULID_LEN)
end

local ulid_frombin = function(bin)
    if type(bin) ~= 'string' or #bin ~= ULID_LEN then
        error('frombin(bin)')
    end
    local u = ffi.new(ulid_t)
    ffi.copy(u, bin, ULID_LEN)
    return u
end

local ulid_isnil = function(u)
    if not is_ulid(u) then
        return error('Usage: ulid:isnil()')
    end
    return builtin.tt_ulid_is_nil(u)
end

local function error_convert_arg(arg_index)
    local err_fmt = 'incorrect value to convert to ulid as %d argument'
    error(err_fmt:format(arg_index), 3)
end

local ulid_eq = function(lhs, rhs)
    if not is_ulid(lhs) then
        lhs, rhs = rhs, lhs
    elseif is_ulid(rhs) then
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

local ulid_new = function()
    local u = ffi.new(ulid_t)
    ulid_create_checked(u)
    return u
end

local ulid_new_bin = function()
    local ulidbuf = ulid_stash_take()
    ulid_create_checked(ulidbuf)
    local res = ulid_tobin(ulidbuf)
    ulid_stash_put(ulidbuf)
    return res
end

local ulid_new_str = function()
    local ulidbuf = ulid_stash_take()
    ulid_create_checked(ulidbuf)
    local res = ulid_tostring(ulidbuf)
    ulid_stash_put(ulidbuf)
    return res
end

local ulid_cmp = function(lhs, rhs)
    local buf
    if is_ulid(lhs) then
        if is_ulid(rhs) then
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

return setmetatable({
    NULL        = builtin.ulid_nil;
    new         = ulid_new;
    fromstr     = ulid_fromstr;
    frombin     = ulid_frombin;
    bin         = ulid_new_bin;   -- shortcut for new():bin()
    str         = ulid_new_str;   -- shortcut for new():str()
    is_ulid     = is_ulid;
}, {
    __call = ulid_new; -- shortcut for new()
})
