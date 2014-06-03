-- log.lua
--
local ffi = require('ffi')
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
]]

local function say(level, fmt, ...)
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
    ffi.C._say(level, file, line, nil, str)
end

return {
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
}
