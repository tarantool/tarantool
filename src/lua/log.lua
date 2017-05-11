local ffi   = require('ffi')
local fio   = require('fio')
local fun   = require('fun')
local errno = require('errno')
local debug = require('debug')

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

local S_FATAL    = ffi.C.S_FATAL
local S_SYSERROR = ffi.C.S_SYSERROR
local S_ERROR    = ffi.C.S_ERROR
local S_CRIT     = ffi.C.S_CRIT
local S_WARN     = ffi.C.S_WARN
local S_INFO     = ffi.C.S_INFO
local S_DEBUG    = ffi.C.S_DEBUG

local debug_getinfo = debug.getinfo

local log_level_name = setmetatable({
    crit     = S_CRIT,
    info     = S_INFO,
    warn     = S_WARN,
    debug    = S_DEBUG,
    error    = S_ERROR,
    fatal    = S_FATAL,
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

local function get_traceback_iter(depth)
    depth = depth or 1

    return function()
        local info = debug_getinfo(depth)
        assert(type(info) == 'nil' or type(info) == 'table')
        if info == nil then
            return nil
        end
        depth = depth + 1
        return depth, {
            line = info.currentline or 0,
            what = info.what or 'undef',
            file = info.source or info.src or 'eval',
            name = info.name,
        }
    end
end

local logger_object_methods = {
    _get_level = function(self)
        return self.level
    end,
    _set_level = function(self, level)
        self.level = level
    end,
    _log = function(self, depth, level, fmt, ...)
        depth = depth or 0
        if self:_get_level() < level then
            return
        end
        local syserror = (level == S_SYSERROR and errno.strerror() or nil)
        if select('#', ...) ~= 0 then
            -- don't waste time on string.format if we weren't passing any args
            local stat
            stat, fmt = pcall(string.format, fmt, ...)
            if not stat then
                error(fmt, 4 + depth)
            end
        end
        local frame = debug_getinfo(4 + depth, "Sl")
        local line, file = 0, 'eval'
        if type(frame) == 'table' then
            line = frame.currentline or 0
            file = frame.source or frame.src or 'eval'
        end
        ffi.C._say(level, file, line, syserror, "%s", fmt)
    end,
    _trace = function(self, output_level, depth)
        output_level = output_level or S_ERROR
        for _, fr in get_traceback_iter((depth or 0) + 4) do
            local name = ''
            if fr.name ~= nil then
                name = (" function '%s'"):format(fr.name)
            end
            self:_log(1, output_level, "[%-4s]%s at <%s:%d>",
                      fr.what, name, fr.file, fr.line)
        end
    end,
    cfg = function(self, options)
        if options == nil and self.is_configured then
            return
        end
        options = fun.iter(options or {}):tomap()
        -- level configuration
        local level = options.level
        if level ~= nil then
            if type(options.level) == 'string' then
                level = log_level_name[level]
                if level == nil then
                    error('bad option level (bad logging level)')
                end
            elseif type(level) == 'number' then
                if level < S_SYSERROR or level > S_DEBUG then
                    error('bad option level (bad logging level)')
                end
            else
                error('bad option level (bad logging level)')
            end
            self:_set_level(level)
        elseif not self.is_configured then
            self:_set_level(S_INFO)
        end
        -- backtrace configuration
        local backtrace_on_error = options.backtrace_on_error
        if type(backtrace_on_error) == 'boolean' then
            self.backtrace_on_error = backtrace_on_error
        elseif not self.is_configured then
            self.backtrace_on_error = false
        end
        self.is_configured = true
    end,
    warn = function(self, fmt, ...)
        self:_log(nil, S_WARN, fmt, ...)
    end,
    info = function(self, fmt, ...)
        self:_log(nil, S_INFO, fmt, ...)
    end,
    debug = function(self, fmt, ...)
        self:_log(nil, S_DEBUG, fmt, ...)
    end,
    error = function(self, fmt, ...)
        self:_log(nil, S_ERROR, fmt, ...)
        if self.backtrace_on_error then self:_trace(S_ERROR) end
    end,
    syserror = function(self, fmt, ...)
        self:_log(nil, S_SYSERROR, fmt, ...)
        if self.backtrace_on_error then self:_trace(S_SYSERROR) end
    end,
    trace = function(self, log_level, level)
        level = level or 1
        if type(log_level) == 'string' then
            log_level = log_level_name[log_level]
            if log_level == nil then
                error('bad argument #1 (log level expected)', 2)
            end
        elseif type(log_level) ~= 'number' then
            log_level = S_ERROR
        end
        self:_trace(log_level, level)
    end
}

-- global table with all logger objects
local logger_object_list = {}

local function logger_object_new(options)
    options = options or {}
    -- prepare logger name
    local name = options.name
    if name == nil then
        name = debug_getinfo(2).source
        if name == nil then
            name = 'internal'
        else
            name = fio.basename(name)
        end
    end
    -- find and return logger, if it exists
    local logger_object = logger_object_list[name]
    if logger_object ~= nil then
        return logger_object
    end
    -- prepare logger object
    local logger_object = setmetatable({
        name = name,
    }, {
        __index = logger_object_methods
    })
    -- let's configure logger
    logger_object:cfg(options)
    return logger_object
end

local logger_default = logger_object_new({ name = 'default' })

logger_default._set_level = function(self, level)
    return ffi.C.say_set_log_level(level)
end

logger_default._get_level = function(self)
    return ffi.C.log_level
end

local logger_default_pid = function() return tonumber(ffi.C.log_pid) end

local compat_warning_said = false
local compat_v16 = {
    logger_pid = function()
        if not compat_warning_said then
            compat_warning_said = true
            logger_default:warn(
                'logger_pid() is deprecated, please use pid() instead'
            )
        end
        return log_default_pid()
    end;
}

return setmetatable({
    -- say for level functions
    syserror = function(...) logger_default:syserror(...) end,
    error    = function(...) logger_default:error(...)    end,
    warn     = function(...) logger_default:warn(...)     end,
    info     = function(...) logger_default:info(...)     end,
    debug    = function(...) logger_default:debug(...)    end,
    trace    = function(...) logger_default:trace(...)    end,
    -- routines
    pid    = logger_default_pid,
    rotate = function() ffi.C.say_logrotate(0)          end,
    -- proxy objects configuration
    new    = logger_object_new,
    -- level configuration
    level_names = log_level_name
    level       = function(lvl)
        if lvl == nil then
            return logger_default:_get_level()
        end
        return logger_default:cfg({ level = lvl })
    end,
    _log        = function(...) logger_default:_log(...)     end,
}, {
    __index = compat_v16,
})
