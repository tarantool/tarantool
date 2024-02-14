-- error.lua (internal file)

local ffi = require('ffi')
local json = require('json')
local msgpack = require('msgpack')
local compat = require('compat')

local mp_decode = msgpack.decode_unchecked

ffi.cdef[[
struct type_info;

enum {
    DIAG_ERRMSG_MAX = 512,
    DIAG_FILENAME_MAX = 256
};

typedef void (*error_f)(struct error *e);

struct error_field {
    char *_data;
    uint32_t _size;
    char *_name;
};

struct error_payload {
    int _count;
    struct error_field **_fields;
};

struct error {
    error_f _destroy;
    error_f _raise;
    error_f _log;
    const struct type_info *_type;
    int64_t _refs;
    int _saved_errno;
    int code;
    struct error_payload _payload;
    /** Line number. */
    unsigned _line;
    /** Source file name. */
    char _file[DIAG_FILENAME_MAX];
    /**
     * Error description. Points to the static buffer `_errmsg_buf' if the
     * message fits into it, or to the dynamic buffer otherwise.
     */
    char *_errmsg;
    char _errmsg_buf[DIAG_ERRMSG_MAX];
    struct error *_cause;
    struct error *_effect;
};

int
error_set_prev(struct error *e, struct error *prev);

const char *
box_error_custom_type(const struct error *e);

void
error_ref(struct error *e);

void
error_unref(struct error *e);

const struct error_field *
error_find_field(const struct error *e, const char *name);
]]

local function error_base_type(err)
    return ffi.string(err._type.name)
end

local function error_type(err)
    local res = ffi.C.box_error_custom_type(err)
    if res ~= nil then
        return ffi.string(res)
    end
    return error_base_type(err)
end

local function error_message(err)
    return ffi.string(err._errmsg)
end

local function error_trace(err)
    if err._file[0] == 0 then
        return {}
    end
    return {
        { file = ffi.string(err._file), line = tonumber(err._line) };
    }
end

local function error_errno(err)
    local e = err._saved_errno
    if e == 0 then
        return nil
    end
    return e
end

local function error_prev(err)
    local e = err._cause;
    if e ~= nil then
        ffi.C.error_ref(e)
        e = ffi.gc(e, ffi.C.error_unref)
        return e
    else
        return nil
    end
end

local function error_set_prev(err, prev)
    -- First argument must be error.
    if not ffi.istype('struct error', err) then
        error("Usage: error1:set_prev(error2)")
    end
    -- Second argument must be error or nil.
    if not ffi.istype('struct error', prev) and prev ~= nil then
        error("Usage: error1:set_prev(error2)")
    end
    local ok = ffi.C.error_set_prev(err, prev);
    if ok ~= 0 then
        error("Cycles are not allowed")
    end
end

local error_fields = {
    ["type"]        = error_type;
    ["message"]     = error_message;
    ["trace"]       = error_trace;
    ["errno"]       = error_errno;
    ["prev"]        = error_prev;
    ["base_type"]   = error_base_type
}

local function error_unpack(err)
    if not ffi.istype('struct error', err) then
        error("Usage: error:unpack()")
    end
    local result = {code = err.code}
    for key, getter in pairs(error_fields)  do
        result[key] = getter(err)
    end
    local payload = err._payload
    local fields = payload._fields
    for i = 0, payload._count - 1 do
        local f = fields[i]
        result[ffi.string(f._name)] = mp_decode(f._data)
    end
    -- Hide redundant fields from unpack output (gh-9101).
    if compat.box_error_unpack_type_and_code:is_new() then
        result.custom_type = nil
        result.base_type = nil
        if result.code == 0 then
            result.code = nil
        end
    end
    return result
end

local function error_raise(err)
    if not ffi.istype('struct error', err) then
        error("Usage: error:raise()")
    end
    error(err)
end

local function error_match(err, ...)
    if not ffi.istype('struct error', err) then
        error("Usage: error:match()")
    end
    return string.match(error_message(err), ...)
end

--
-- Serialize an error (including the whole error stack) to its table
-- representation.
-- @param err error object
-- @return error converted to table representation
local function error_serialize(err)
    if compat.box_error_serialize_verbose:is_new() then
        local res = error_unpack(err)
        local cur = res
        while cur.prev ~= nil do
            cur.prev = error_unpack(cur.prev)
            cur = cur.prev
        end
        return res
    else
        return error_message(err)
    end
end

local error_methods

local function error_autocomplete(err)
    local err_unpacked = {}

    local cur_err = err
    while cur_err ~= nil do
        for key, val in pairs(cur_err:unpack()) do
            if err_unpacked[key] == nil then
                err_unpacked[key] = val
            end
        end
        cur_err = cur_err.prev
    end

    for key, method in pairs(error_methods) do
        if not key:startswith('__') then
            err_unpacked[key] = method
        end
    end
    return err_unpacked
end

error_methods = {
    ["unpack"] = error_unpack;
    ["raise"] = error_raise;
    ["match"] = error_match; -- Tarantool 1.6 backward compatibility
    ["__serialize"] = error_serialize;
    ["set_prev"] = error_set_prev;
    ["__autocomplete"] = error_autocomplete;
}

local function error_index(err, key)
    local method = error_methods[key]
    if method ~= nil then
        return method
    end
    local getter = error_fields[key]
    if getter ~= nil then
        return getter(err)
    end
    -- Look for a payload field, starting from the topmost error in the stack.
    local cur_err = err
    while cur_err ~= nil do
        local f = ffi.C.error_find_field(cur_err, key)
        if f ~= nil then
            return mp_decode(f._data)
        end
        cur_err = cur_err.prev
    end
end

local function error_concat(lhs, rhs)
    if ffi.istype('struct error', lhs) then
        return tostring(lhs) .. rhs
    elseif ffi.istype('struct error', rhs) then
        return lhs .. tostring(rhs)
    else
       error('error_mt.__concat(): neither of args is an error')
    end
end

--
-- Convert error to its string representation without accounting for the
-- error's cause.
-- @param err error object
-- @return error's string representation
local function error_to_string_wo_prev(err)
    local err_unpacked = error_unpack(err)
    err_unpacked.message = nil
    err_unpacked.prev = nil
    return string.format('%s %s', err.message, json.encode(err_unpacked))
end

--
-- Convert an error to its string representation accounting for the whole error
-- stack.
-- @param err error object
-- @return error's string representation
local function error_to_string(err)
    if compat.box_error_serialize_verbose:is_old() then
        return error_message(err)
    end

    local cur = err
    local error_stack = {}
    while cur ~= nil do
        table.insert(error_stack, 1, error_to_string_wo_prev(cur))
        cur = cur.prev
    end
    return table.concat(error_stack, '\n')
end

local error_mt = {
    __index = error_index;
    __tostring = error_to_string;
    __concat = error_concat;
};

ffi.metatype('struct error', error_mt);

return error
