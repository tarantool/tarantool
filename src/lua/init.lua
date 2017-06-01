-- init.lua -- internal file

local ffi = require('ffi')
ffi.cdef[[
struct type;
struct method;
struct error;

enum ctype {
    CTYPE_VOID = 0,
    CTYPE_INT,
    CTYPE_CONST_CHAR_PTR
};

struct type {
    const char *name;
    const struct type *parent;
    const struct method *methods;
};

enum {
    DIAG_ERRMSG_MAX = 512,
    DIAG_FILENAME_MAX = 256
};

typedef void (*error_f)(struct error *e);

struct error {
    error_f _destroy;
    error_f _raise;
    error_f _log;
    const struct type *_type;
    int _refs;
    /** Line number. */
    unsigned _line;
    /* Source file name. */
    char _file[DIAG_FILENAME_MAX];
    /* Error description. */
    char _errmsg[DIAG_ERRMSG_MAX];
};

enum { METHOD_ARG_MAX = 8 };

struct method {
    const struct type *owner;
    const char *name;
    enum ctype rtype;
    enum ctype atype[METHOD_ARG_MAX];
    int nargs;
    bool isconst;

    union {
        /* Add extra space to get proper struct size in C */
        void *_spacer[2];
    };
};

char *
exception_get_string(struct error *e, const struct method *method);
int
exception_get_int(struct error *e, const struct method *method);

double
tarantool_uptime(void);
typedef int32_t pid_t;
pid_t getpid(void);
]]

local REFLECTION_CACHE = {}

local function reflection_enumerate(err)
    local key = tostring(err._type)
    local result = REFLECTION_CACHE[key]
    if result ~= nil then
        return result
    end
    result = {}
    -- See type_foreach_method() in reflection.h
    local t = err._type
    while t ~= nil do
        local m = t.methods
        while m.name ~= nil do
            result[ffi.string(m.name)] = m
            m = m + 1
        end
        t = t.parent
    end
    REFLECTION_CACHE[key] = result
    return result
end

local function reflection_get(err, method)
    if method.nargs ~= 0 then
        return nil -- NYI
    end
    if method.rtype == ffi.C.CTYPE_INT then
        return tonumber(ffi.C.exception_get_int(err, method))
    elseif method.rtype == ffi.C.CTYPE_CONST_CHAR_PTR then
        local str = ffi.C.exception_get_string(err, method)
        if str == nil then
            return nil
        end
        return ffi.string(str)
    end
end

local function error_type(err)
    return ffi.string(err._type.name)
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

local error_fields = {
    ["type"]        = error_type;
    ["message"]     = error_message;
    ["trace"]       = error_trace;
}

local function error_unpack(err)
    if not ffi.istype('struct error', err) then
        error("Usage: error:unpack()")
    end
    local result = {}
    for key, getter in pairs(error_fields)  do
        result[key] = getter(err)
    end
    for key, getter in pairs(reflection_enumerate(err)) do
        local value = reflection_get(err, getter)
        if value ~= nil then
            result[key] = value
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

local function error_serialize(err)
    -- Return an error message only in admin console to keep compatibility
    return error_message(err)
end

local error_methods = {
    ["unpack"] = error_unpack;
    ["raise"] = error_raise;
    ["match"] = error_match; -- Tarantool 1.6 backward compatibility
    ["__serialize"] = error_serialize;
}

local function error_index(err, key)
    local getter = error_fields[key]
    if getter ~= nil then
        return getter(err)
    end
    getter = reflection_enumerate(err)[key]
    if getter ~= nil and getter.nargs == 0 then
        local val = reflection_get(err, getter)
        if val ~= nil then
            return val
        end
    end
    return error_methods[key]
end

local error_mt = {
    __index = error_index;
    __tostring = error_message;
};

ffi.metatype('struct error', error_mt);

dostring = function(s, ...)
    local chunk, message = loadstring(s)
    if chunk == nil then
        error(message, 2)
    end
    return chunk(...)
end

local function uptime()
    return tonumber(ffi.C.tarantool_uptime());
end

local function pid()
    return tonumber(ffi.C.getpid())
end

return {
    uptime = uptime;
    pid = pid;
}
