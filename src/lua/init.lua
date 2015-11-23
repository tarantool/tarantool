-- init.lua -- internal file

local ffi = require('ffi')
ffi.cdef[[
struct type;
struct method;
struct error;

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

/* TODO: remove these declarations */
const struct error *
box_error_last(void);
const char *
box_error_message(const struct error *);

double
tarantool_uptime(void);
typedef int32_t pid_t;
pid_t getpid(void);
]]

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

-- TODO: Use reflection
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
    return result
end

local function error_raise(err)
    if not ffi.istype('struct error', err) then
        error("Usage: error:raise()")
    end
    error(err)
end

local function error_serialize(err)
    -- Return an error message only in admin console to keep compatibility
    return error_message(err)
end

local error_methods = {
    ["unpack"] = error_unpack;
    ["raise"] = error_raise;
    ["__serialize"] = error_serialize;
}

local function error_index(err, key)
    local getter = error_fields[key]
    if getter ~= nil then
        return getter(err)
    end
    return error_methods[key]
end

local error_mt = {
    __index = error_index;
    __tostring = error_message;
};

ffi.metatype('struct error', error_mt);

-- Override pcall to support Tarantool exceptions
local pcall_lua = pcall

local function pcall_wrap(status, ...)
    if status == true then
        return true, ...
    end
    if ffi.istype('struct error', (...)) then
        -- Return the Tarantool error as string to keep compatibility.
        -- Caller should check box.error.last() to get additional information.
        return false, tostring((...))
    elseif ... == 'C++ exception' then
        return false, ffi.string(ffi.C.box_error_message(ffi.C.box_error_last()))
    end
    return status, ...
end
pcall = function(fun, ...)
    return pcall_wrap(pcall_lua(fun, ...))
end

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
