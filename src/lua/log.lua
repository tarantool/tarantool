-- log.lua
--
local ffi = require('ffi')
ffi.cdef[[
char *
tarantool_error_message(void);
typedef int32_t pid_t;
pid_t getpid(void);
]]

local pcall_lua = pcall

local function pcall_wrap(status, ...)
    if status == false and ... == 'C++ exception' then
        return false, ffi.string(ffi.C.tarantool_error_message())
    end
    return status, ...
end
local pcall = function(fun, ...)
    return pcall_wrap(pcall_lua(fun, ...))
end

local dostring = function(s, ...)
    local chunk, message = loadstring(s)
    if chunk == nil then
        error(message, 2)
    end
    return chunk(...)
end

local function pid()
    return tonumber(ffi.C.getpid())
end


ffi.cdef[[
    typedef void (*sayfunc_t)(int level, const char *filename, int line,
               const char *error, const char *format, ...);

    extern sayfunc_t _say;
    enum say_level {
        S_FATAL,
        S_SYSERROR,
        S_ERROR,
        S_CRIT,
        S_WARN,
        S_INFO,
        S_DEBUG
    };

    pid_t logger_pid;
]]

local function say(level, fmt, ...)
    local debug = require('debug')
    local str = string.format(fmt, ...)
    local frame = debug.getinfo(3, "Sl")
    local line = 0
    local file = 'eval'
    if type(frame) == 'table' then
        line = frame.currentline
        if not line then
            line = 0
        end
        file = frame.short_src
        if not file then
            file = frame.src
        end
        if not file then
            file = 'eval'
        end
    end
    ffi.C._say(level, file, line, nil, "%s", str)
end

package.loaded.log = {
    warn = function (fmt, ...)
        say(ffi.C.S_WARN, fmt, ...)
    end,

    info = function (fmt, ...)
        say(ffi.C.S_INFO, fmt, ...)
    end,

    debug = function (fmt, ...)
        say(ffi.C.S_DEBUG, fmt, ...)
    end,

    error = function (fmt, ...)
        say(ffi.C.S_ERROR, fmt, ...)
    end,

    logger_pid = function()
        return tonumber(ffi.C.logger_pid)
    end
}
