-- log.lua
--
local ffi = require('ffi')
ffi.cdef[[
    typedef void (*sayfunc_t)(int level, const char *filename, int line,
               const char *error, const char *format, ...);

    void
    say_set_log_level(int new_level);

    void
    say_set_log_format(enum say_format format);

    int
    say_check_cfg(const char *log,
                  int level,
                  int nonblock,
                  const char *format);

    extern void
    say_logger_init(const char *init_str, int level, int nonblock,
                    const char *format);

    extern bool
    say_logger_initialized(void);

    extern sayfunc_t _say;
    extern struct ev_loop;
    extern struct ev_signal;

    extern void
    say_logrotate(struct ev_loop *, struct ev_signal *, int);

    enum say_level {
        S_FATAL,
        S_SYSERROR,
        S_ERROR,
        S_CRIT,
        S_WARN,
        S_INFO,
        S_VERBOSE,
        S_DEBUG
    };

    enum say_format {
        SF_PLAIN,
        SF_JSON
    };
    pid_t log_pid;
    extern int log_level;
    extern int log_format;
]]

local S_CRIT = ffi.C.S_CRIT
local S_WARN = ffi.C.S_WARN
local S_INFO = ffi.C.S_INFO
local S_VERBOSE = ffi.C.S_VERBOSE
local S_DEBUG = ffi.C.S_DEBUG
local S_ERROR = ffi.C.S_ERROR

local json = require("json").new()
json.cfg{
    encode_invalid_numbers = true,
    encode_load_metatables = true,
    encode_use_tostring    = true,
    encode_invalid_as_nil  = true,
}

local special_fields = {
    "file",
    "level",
    "pid",
    "line",
    "cord_name",
    "fiber_name",
    "fiber_id",
    "error_msg"
}

-- Map format number to string.
local fmt_num2str = {
    [ffi.C.SF_PLAIN]    = "plain",
    [ffi.C.SF_JSON]     = "json",
}

-- Map format string to number.
local fmt_str2num = {
    ["plain"]           = ffi.C.SF_PLAIN,
    ["json"]            = ffi.C.SF_JSON,
}

-- Logging levels symbolic representation.
local log_level_keys = {
    ['fatal']       = ffi.C.S_FATAL,
    ['syserror']    = ffi.C.S_SYSERROR,
    ['error']       = ffi.C.S_ERROR,
    ['crit']        = ffi.C.S_CRIT,
    ['warn']        = ffi.C.S_WARN,
    ['info']        = ffi.C.S_INFO,
    ['verbose']     = ffi.C.S_VERBOSE,
    ['debug']       = ffi.C.S_DEBUG,
}

local function log_level_list()
    local keyset = {}
    for k in pairs(log_level_keys) do
        keyset[#keyset + 1] = k
    end
    return table.concat(keyset, ',')
end

-- Default options. The keys are part of
-- user API , so change with caution.
local default_cfg = {
    log             = nil,
    nonblock        = nil,
    level           = S_INFO,
    format          = fmt_num2str[ffi.C.SF_PLAIN],
}

local log_cfg = table.copy(default_cfg)

-- Name mapping from box to log module and
-- back. Make sure all required fields
-- are covered!
local log2box_keys = {
    ['log']             = 'log',
    ['nonblock']        = 'log_nonblock',
    ['level']           = 'log_level',
    ['format']          = 'log_format',
}

-- Main routine which pass data to C logging code.
local function say(level, fmt, ...)
    if ffi.C.log_level < level then
        -- don't waste cycles on debug.getinfo()
        return
    end
    local type_fmt = type(fmt)
    local format = "%s"
    if select('#', ...) ~= 0 then
        local stat
        stat, fmt = pcall(string.format, fmt, ...)
        if not stat then
            error(fmt, 3)
        end
    elseif type_fmt == 'table' then
        -- ignore internal keys
        for _, field in ipairs(special_fields) do
            fmt[field] = nil
        end
        fmt = json.encode(fmt)
        if ffi.C.log_format == ffi.C.SF_JSON then
            -- indicate that message is already encoded in JSON
            format = fmt_num2str[ffi.C.SF_JSON]
        end
    elseif type_fmt ~= 'string' then
        fmt = tostring(fmt)
    end

    local debug = require('debug')
    local frame = debug.getinfo(3, "Sl")
    local line, file = 0, 'eval'
    if type(frame) == 'table' then
        line = frame.currentline or 0
        file = frame.short_src or frame.src or 'eval'
    end

    ffi.C._say(level, file, line, nil, format, fmt)
end

-- Just a syntactic sugar over say routine.
local function say_closure(lvl)
    return function (fmt, ...)
        say(lvl, fmt, ...)
    end
end

local log_error = say_closure(S_ERROR)
local log_warn = say_closure(S_WARN)
local log_info = say_closure(S_INFO)
local log_verbose = say_closure(S_VERBOSE)
local log_debug = say_closure(S_DEBUG)

-- Rotate log (basically reopen the log file and
-- start writting into it).
local function log_rotate()
    ffi.C.say_logrotate(nil, nil, 0)
end

-- Returns pid of a pipe process.
local function log_pid()
    return tonumber(ffi.C.log_pid)
end

local ratelimit_enabled = true

local function ratelimit_enable()
    ratelimit_enabled = true
end

local function ratelimit_disable()
    ratelimit_enabled = false
end

local Ratelimit = {
    interval = 60,
    burst = 10,
    emitted = 0,
    suppressed = 0,
    start = 0,
}

local function ratelimit_new(object)
    return Ratelimit:new(object)
end

function Ratelimit:new(object)
    object = object or {}
    setmetatable(object, self)
    self.__index = self
    return object
end

function Ratelimit:check()
    if not ratelimit_enabled then
        return 0, true
    end

    local clock = require('clock')
    local now = clock.monotonic()
    local saved_suppressed = 0
    if now > self.start + self.interval then
        saved_suppressed = self.suppressed
        self.suppressed = 0
        self.emitted = 0
        self.start = now
    end

    if self.emitted < self.burst then
        self.emitted = self.emitted + 1
        return saved_suppressed, true
    end
    self.suppressed = self.suppressed + 1
    return saved_suppressed, false
end

function Ratelimit:log_check(lvl)
    local suppressed, ok = self:check()
    if lvl >= S_WARN and suppressed > 0 then
        say(S_WARN, '%d messages suppressed due to rate limiting', suppressed)
    end
    return ok
end

function Ratelimit:log(lvl, fmt, ...)
    if self:log_check(lvl) then
        say(lvl, fmt, ...)
    end
end

local function log_ratelimited_closure(lvl)
    return function(self, fmt, ...)
        self:log(lvl, fmt, ...)
    end
end

Ratelimit.log_crit = log_ratelimited_closure(S_CRIT)

local option_types = {
    log = 'string',
    nonblock = 'boolean',
    level = 'number, string',
    format = 'string',
}

local log_initialized = false

-- Convert cfg options to types suitable for ffi say_ functions.
local function log_C_cfg(cfg)
    local cfg_C = table.copy(cfg)
    if type(cfg.level) == 'string' then
        cfg_C.level = log_level_keys[cfg.level]
    end
    local nonblock
    if cfg.nonblock ~= nil then
        nonblock = cfg.nonblock and 1 or 0
    else
        nonblock = -1
    end
    cfg_C.nonblock = nonblock
    return cfg_C
end

-- Check cfg is valid and thus can be applied
local function log_check_cfg(cfg)
    if type(cfg.level) == 'string' and
       log_level_keys[cfg.level] == nil then
        local err = ("expected %s"):format(log_level_list())
        box.error(box.error.CFG, "log_level", err)
    end

    if log_initialized then
        if log_cfg.log ~= cfg.log then
            box.error(box.error.RELOAD_CFG, 'log');
        end
        if log_cfg.nonblock ~= cfg.nonblock then
            box.error(box.error.RELOAD_CFG, 'log_nonblock');
        end
    end

    local cfg_C = log_C_cfg(cfg)
    if ffi.C.say_check_cfg(cfg_C.log, cfg_C.level,
                           cfg_C.nonblock, cfg_C.format) ~= 0 then
        box.error()
    end
end

-- Update box.internal.cfg on log config changes
local function box_cfg_update(key)
    if key == nil then
        for km, kb  in pairs(log2box_keys) do
            box.internal.update_cfg(kb, log_cfg[km])
        end
    else
        box.internal.update_cfg(log2box_keys[key], log_cfg[key])
    end
end

local function set_log_level(level)
    box.internal.check_cfg_option_type(option_types.level, 'level', level)
    local cfg = table.copy(log_cfg)
    cfg.level = level
    log_check_cfg(cfg)

    local cfg_C = log_C_cfg(cfg)
    ffi.C.say_set_log_level(cfg_C.level)
    log_cfg.level = level

    box_cfg_update('level')

    log_debug("log: level set to %s", level)
end

local function set_log_format(format)
    box.internal.check_cfg_option_type(option_types.format, 'format', format)
    local cfg = table.copy(log_cfg)
    cfg.format = format
    log_check_cfg(cfg)

    ffi.C.say_set_log_format(fmt_str2num[format])
    log_cfg.format = format

    box_cfg_update('format')

    log_debug("log: format set to '%s'", format)
end

local function log_configure(self, cfg)
    cfg = box.internal.prepare_cfg(cfg, default_cfg, option_types)
    box.internal.merge_cfg(cfg, log_cfg);

    log_check_cfg(cfg)
    local cfg_C = log_C_cfg(cfg)
    ffi.C.say_logger_init(cfg_C.log, cfg_C.level,
                          cfg_C.nonblock, cfg_C.format)
    log_initialized = true

    for o in pairs(option_types) do
        log_cfg[o] = cfg[o]
    end

    box_cfg_update()

    log_debug("log.cfg({log=%s, level=%s, nonblock=%s, format=%s})",
              cfg.log, cfg.level, cfg.nonblock, cfg.format)
end

local function box_to_log_cfg()
    return {
        log = box.cfg.log,
        level = box.cfg.log_level,
        format = box.cfg.log_format,
        nonblock = box.cfg.log_nonblock,
    }
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

local log = {
    warn = log_warn,
    info = log_info,
    verbose = log_verbose,
    debug = log_debug,
    error = log_error,
    rotate = log_rotate,
    pid = log_pid,
    level = set_log_level,
    log_format = set_log_format,
    cfg = setmetatable(log_cfg, {
        __call = log_configure,
    }),
    box_api = {
        cfg = function() log_configure(log_cfg, box_to_log_cfg()) end,
        cfg_check = function() log_check_cfg(box_to_log_cfg()) end,
    },
    internal = {
        ratelimit = {
            new = ratelimit_new,
            enable = ratelimit_enable,
            disable = ratelimit_disable,
        },
    }
}

setmetatable(log, {
    __serialize = function(self)
        local res = table.copy(self)
        res.box_api = nil
        return setmetatable(res, {})
    end,
    __index = compat_v16;
})

return log
