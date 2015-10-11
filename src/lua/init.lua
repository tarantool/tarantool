-- init.lua -- internal file

-- Override pcall to support Tarantool exceptions

local ffi = require('ffi')
ffi.cdef[[
struct error;

typedef struct error box_error_t;
const box_error_t *
box_error_last(void);
const char *
box_error_message(const box_error_t *);
double
tarantool_uptime(void);
typedef int32_t pid_t;
pid_t getpid(void);
]]

local pcall_lua = pcall

local function pcall_wrap(status, ...)
    if status == false and ... == 'C++ exception' then
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
