-- error.lua (internal file)

local ffi = require('ffi')
local msgpack = require('msgpack')

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
    /* Source file name. */
    char _file[DIAG_FILENAME_MAX];
    /* Error description. */
    char _errmsg[DIAG_ERRMSG_MAX];
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
    ffi.C.error_ref(err)
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

local function error_serialize(err)
    -- Return an error message only in admin console to keep compatibility
    return error_message(err)
end

local error_methods = {
    ["unpack"] = error_unpack;
    ["raise"] = error_raise;
    ["match"] = error_match; -- Tarantool 1.6 backward compatibility
    ["__serialize"] = error_serialize;
    ["set_prev"] = error_set_prev;
}

local function error_index(err, key)
    local getter = error_fields[key]
    if getter ~= nil then
        return getter(err)
    end
    local f = ffi.C.error_find_field(err, key)
    if f ~= nil then
        return mp_decode(f._data)
    end
    return error_methods[key]
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

local error_mt = {
    __index = error_index;
    __tostring = error_message;
    __concat = error_concat;
};

ffi.metatype('struct error', error_mt);

return error
