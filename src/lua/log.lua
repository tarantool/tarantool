-- log.lua
--
local ffi = require('ffi')
ffi.cdef[[
    typedef void (*sayfunc_t)(int level, const char *filename, int line,
               const char *error, const char *format, ...);
    void
    say_set_log_level(int new_level);

    extern sayfunc_t _say;
    extern void say_logrotate(int);

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
    extern int log_level;
]]

local S_WARN  = ffi.C.S_WARN
local S_INFO  = ffi.C.S_INFO
local S_DEBUG = ffi.C.S_DEBUG
local S_ERROR = ffi.C.S_ERROR

local function say(level, fmt, ...)
    if ffi.C.log_level < level then
        -- don't waste cycles on debug.getinfo()
        return
    end
    local debug = require('debug')
    local str = string.format(tostring(fmt), ...)
    local frame = debug.getinfo(3, "Sl")
    local line = 0
    local file = 'eval'
    if type(frame) == 'table' then
        line = frame.currentline or 0
        file = frame.short_src or frame.src or 'eval'
    end
    ffi.C._say(level, file, line, nil, "%s", str)
end

local function say_closure(lvl)
    return function (fmt, ...)
        say(lvl, fmt, ...)
    end
end

return {
    warn = say_closure(S_WARN),
    info = say_closure(S_INFO),
    debug = say_closure(S_DEBUG),
    error = say_closure(S_ERROR),

    rotate = function()
        ffi.C.say_logrotate(0)
    end,

    logger_pid = function()
        return tonumber(ffi.C.logger_pid)
    end,

    level = function(level)
        return ffi.C.say_set_log_level(level)
    end,
}
