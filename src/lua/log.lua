local ffi   = require('ffi')
local yaml  = require('yaml').new()
local errno = require('errno')
local debug = require('debug')

yaml.cfg{
    encode_invalid_numbers = true,
    encode_use_tostring    = true,
    encode_invalid_as_nil  = true,
    encode_load_metatables = true,
}

ffi.cdef[[
    typedef void (*sayfunc_t)(int level, const char *filename, int line,
                              const char *error, const char *format, ...);
    void say_set_log_level(int new_level);

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

    pid_t log_pid;
    extern int log_level;
]]

local S_SYSERROR = ffi.C.S_SYSERROR
local S_ERROR    = ffi.C.S_ERROR
local S_WARN     = ffi.C.S_WARN
local S_INFO     = ffi.C.S_INFO
local S_DEBUG    = ffi.C.S_DEBUG

local log_level_name = setmetatable({
    warn     = S_WARN,
    info     = S_INFO,
    debug    = S_DEBUG,
    error    = S_ERROR,
    syserror = S_SYSERROR,
}, {
    __index = function(self, name)
        if type(name) == 'number' then
            return name
        end
        if type(name) ~= 'string' then
            return nil
        end
        return rawget(self, string.lower(name))
    end
})

local function say(level, fmt, ...)
    if ffi.C.log_level < level then -- don't waste cycles on debug.getinfo()
        return
    end
    local syserror = nil
    if level == S_SYSERROR then
        syserror = errno.strerror()
    end
    if select('#', ...) ~= 0 then
        -- don't waste time on string.format if we weren't passing any args
        local stat
        stat, fmt = pcall(string.format, fmt, ...)
        if not stat then
            error(fmt, 3)
        end
    end
    local frame = debug.getinfo(3, "Sl")
    local line, file = 0, 'eval'
    if type(frame) == 'table' then
        line = frame.currentline or 0
        file = frame.source or frame.src or 'eval'
    end
    ffi.C._say(level, file, line, syserror, "%s", fmt)
end

local function say_closure(lvl)
    return function (fmt, ...) say(lvl, fmt, ...) end
end

local function log_rotate()
    ffi.C.say_logrotate(0)
end

local function log_level(level)
    if type(level) == 'string' then
        level = log_level_name[level]
        if level == nil then
            error('bad argument #1 (log level expected)', 2)
        end
    end
    return ffi.C.say_set_log_level(level)
end

local function log_pid()
    return tonumber(ffi.C.log_pid)
end

local compat_warning_said = false
local compat_v16 = {
    logger_pid = function()
        if not compat_warning_said then
            compat_warning_said = true
            say(S_WARN, 'logger_pid() is deprecated, please use pid() instead')
        end
        return log_pid()
    end;
}

local log = setmetatable({
    -- say for level functions
    warn     = say_closure(S_WARN),
    info     = say_closure(S_INFO),
    debug    = say_closure(S_DEBUG),
    error    = say_closure(S_ERROR),
    syserror = say_closure(S_SYSERROR),
    -- routines
    pid = log_pid,
    rotate = log_rotate,
    -- level configuration
    level = log_level,
}, {
    __index = compat_v16,
})

local function get_traceback_iter(ldepth)
    ldepth = ldepth or 1

    return function()
        local info = debug.getinfo(ldepth)
        -- print(yaml.encode(info))
        assert(type(info) == 'nil' or type(info) == 'table')
        if info == nil then
            return nil
        end
        ldepth = ldepth + 1
        return ldepth, {
            line = info.currentline or 0,
            what = info.what or 'undef',
            file = info.source or info.src or 'eval',
            name = info.name,
        }
    end
end

local function log_trace(ldepth)
    for _, fr in get_traceback_iter((ldepth or 0) + 3) do
        local name = ''
        if fr.name ~= nil then
            name = (" function '%s'"):format(fr.name)
        end
        log.error("[%-4s]%s at <%s:%d>", fr.what, name, fr.file, fr.line)
    end
end

log.trace = log_trace

return log
