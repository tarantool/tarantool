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

    extern void
    say_from_lua(int level, const char *module, const char *filename, int line,
                 const char *format, ...);

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
        SF_JSON,
    };
    pid_t log_pid;
    extern int log_level;
    extern int log_format;

    extern int log_level_flightrec;
    extern void
    log_write_flightrec_from_lua(int level, const char *filename, int line,
                                 ...);
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
    ["file"]       = true,
    ["level"]      = true,
    ["pid"]        = true,
    ["line"]       = true,
    ["cord_name"]  = true,
    ["fiber_name"] = true,
    ["fiber_id"]   = true,
    ["error_msg"]  = true,
}

local compat = require('compat')
compat.add_option({
    name = 'log_rewrite_special_fields',
    default = 'old',
    obsolete = nil,
    brief = [[
Defines if 'json' logger will rewrite special fields or not. The old
behavior is to rewrite special fields, provided by the user, the new
behavior is to keep the user provided fields.

https://tarantool.io/compat/log_rewrite_special_fields
    ]],
    action = function(is_new)
        if is_new then
            special_fields = {}
        end
    end,
})

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

-- Logging levels string representation.
local log_level_strs = {
    [ffi.C.S_FATAL]    = "FATAL",
    [ffi.C.S_SYSERROR] = "SYSERROR",
    [ffi.C.S_ERROR]    = "ERROR",
    [ffi.C.S_CRIT]     = "CRIT",
    [ffi.C.S_WARN]     = "WARN",
    [ffi.C.S_INFO]     = "INFO",
    [ffi.C.S_VERBOSE]  = "VERBOSE",
    [ffi.C.S_DEBUG]    = "DEBUG",
};

local function log_level_list()
    local keyset = {}
    for k in pairs(log_level_keys) do
        keyset[#keyset + 1] = k
    end
    return table.concat(keyset, ',')
end

-- Default options. The keys are part of
-- user API, so change with caution.
local default_cfg = {
    log               = nil,
    nonblock          = nil,
    level             = S_INFO,
    modules           = nil,
    format            = fmt_num2str[ffi.C.SF_PLAIN],
    fields_order      = nil,
    context_generator = nil,
}

local log_cfg = table.copy(default_cfg)

-- Name mapping from box to log module and
-- back. Make sure all required fields
-- are covered!
local log2box_keys = {
    ['log']               = 'log',
    ['nonblock']          = 'log_nonblock',
    ['level']             = 'log_level',
    ['modules']           = 'log_modules',
    ['format']            = 'log_format',
    ['fields_order']      = 'log_fields_order',
    ['context_generator'] = 'log_context_generator',
}

-- Return level as a number, level must be valid.
local function log_normalize_level(level)
    if type(level) == 'string' then
        return log_level_keys[level]
    end
    return level
end

local box2log_keys = {}

for kl, kb in pairs(log2box_keys) do
    box2log_keys[kb] = kl
end

local function default_context(self, level, file, line)
    local ctx = require('log')._internal.default_context(self)
    ctx.level = log_level_strs[level]
    ctx.file = file
    ctx.line = line

    return ctx
end

-- Main routine which pass data to C logging code.
local function say(self, level, fmt, ...)
    local name = self and self.name
    local module_level = name and log_cfg.modules and log_cfg.modules[name] or
                         log_cfg.level
    if level > log_normalize_level(module_level) and
       level > ffi.C.log_level_flightrec then
        return
    end

    local debug = require('debug')
    local frame = debug.getinfo(3, "Sl")
    local line, file = 0, 'eval'
    if type(frame) == 'table' then
        line = frame.currentline or 0
        file = frame.short_src or frame.src or 'eval'
    end

    local type_fmt = type(fmt)
    local format = "%s"
    local msg, fields_order
    if select('#', ...) ~= 0 then
        local stat
        stat, msg = pcall(string.format, fmt, ...)
        if not stat then
            error(msg, 3)
        end
    elseif type_fmt == 'table' then
        if ffi.C.log_format == ffi.C.SF_JSON then
            -- This closure is needed to provide level for the context.
            -- file and line could be obtained inside the default_context
            -- function, but level is not available there. So we pass it
            -- explicitly. file and line are passed explicitly for consistency.
            local function default_context_closure()
                return default_context(self, level, file, line)
            end

            if log_cfg.context_generator ~= nil then
                local global_context_generator = rawget(
                    _G, log_cfg.context_generator)

                if type(global_context_generator) == 'function' then
                    msg = global_context_generator(default_context_closure)
                elseif box.schema.func.exists(log_cfg.context_generator) then
                    msg = box.func[log_cfg.context_generator]:call({
                        default_context_closure})
                end
            end

            if msg == nil then
                msg = default_context_closure()
                msg.time = msg.time:format('%FT%T.%3f%z')
            end

            -- Use serialization function from the metatable, if any.
            local fmt_mt = getmetatable(fmt)
            if fmt_mt and type(fmt_mt.__serialize) == 'function' then
                fmt = fmt_mt.__serialize(fmt)
                if type(fmt) ~= 'table' then
                    fmt = { message = tostring(fmt) }
                end
            end

            -- Return empty string for an empty table.
            if next(fmt) == nil then
                fmt.message = ''
            end

            -- Set 'message' field if it is absent.
            if fmt.message == nil then
                fmt.message = fmt[1]
                fmt[1] = nil
            end

            -- Merge context and message.
            for k, v in pairs(fmt) do
                -- Ignore internal keys.
                if special_fields[k] == nil then
                    msg[k] = v
                end
            end

            -- Always encode tables as maps.
            setmetatable(msg, json.map_mt)

            if log_cfg.fields_order then
                fields_order = log_cfg.fields_order
            else
                fields_order = {
                    'time',
                    'level',
                    'message',
                    'pid',
                    'cord_name',
                    'fiber_id',
                    'fiber_name',
                    'file',
                    'line',
                    'module',
                }
            end

            -- Indicate that message is already encoded in JSON.
            format = fmt_num2str[ffi.C.SF_JSON]
        else
            msg = table.copy(fmt)
        end

        msg = json.encode(msg, {encode_key_order = fields_order})
    else
        msg = tostring(fmt)
    end

    if level <= ffi.C.log_level_flightrec then
        ffi.C.log_write_flightrec_from_lua(level, file, line, msg)
    end
    if level <= log_normalize_level(module_level) then
        ffi.C.say_from_lua(level, name, file, line, format, msg)
    end
end

-- Just a syntactic sugar over say routine.
local function say_closure(self, lvl)
    return function (fmt, ...)
        say(self, lvl, fmt, ...)
    end
end

local log_error = say_closure(nil, S_ERROR)
local log_warn = say_closure(nil, S_WARN)
local log_info = say_closure(nil, S_INFO)
local log_verbose = say_closure(nil, S_VERBOSE)
local log_debug = say_closure(nil, S_DEBUG)

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
        log_warn('%d messages suppressed due to rate limiting', suppressed)
    end
    return ok
end

function Ratelimit:log(lvl, fmt, ...)
    if self:log_check(lvl) then
        say(nil, lvl, fmt, ...)
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
    modules = 'table',
    format = 'string',
    context_generator = 'string',
    fields_order = 'table',
}

local log_initialized = false

-- Convert cfg options to types suitable for ffi say_ functions.
local function log_C_cfg(cfg)
    local cfg_C = table.copy(cfg)
    local level = cfg.modules and cfg.modules.tarantool or cfg.level
    cfg_C.level = log_normalize_level(level)
    local nonblock
    if cfg.nonblock ~= nil then
        nonblock = cfg.nonblock and 1 or 0
    else
        nonblock = -1
    end
    cfg_C.nonblock = nonblock
    return cfg_C
end

local function box_to_log_cfg(box_cfg)
    local log_cfg = {}
    for kl, kb in pairs(log2box_keys) do
        log_cfg[kl] = box_cfg[kb]
    end
    return log_cfg
end

-- Check that level is a number or a valid string.
local function log_check_level(level, option_name)
    if type(level) == 'string' and log_level_keys[level] == nil then
        local err = ("expected %s"):format(log_level_list())
        box.error(box.error.CFG, option_name, err)
    end
end

-- Check that the 'modules' table contains valid log levels.
local function log_check_modules(modules)
    if modules == nil then
        return
    end
    for name, level in pairs(modules) do
        if type(name) ~= 'string' then
            box.error(box.error.CFG, 'module name', 'should be of type string')
        end
        local option_name = 'log_modules.' .. name
        box.internal.check_cfg_option_type(option_types.level, option_name,
                                           level)
        log_check_level(level, option_name)
    end
end

-- Check cfg is valid and thus can be applied.
local function log_check_cfg(cfg)
    log_check_level(cfg.level, 'log_level')
    log_check_modules(cfg.modules)

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

local function set_log_fields_order(fields_order)
    box.internal.check_cfg_option_type(
        option_types.fields_order, 'fields_order', fields_order)

    if #fields_order == 0 then
        fields_order = nil
    end

    local cfg = table.copy(log_cfg)
    cfg.fields_order = fields_order
    log_check_cfg(cfg)

    log_cfg.fields_order = fields_order

    box_cfg_update('fields_order')

    log_debug("log: fields_order set to '%s'", json.encode(fields_order))
end

local function set_log_context_generator(context_generator)
    box.internal.check_cfg_option_type(
        option_types.context_generator, 'context_generator', context_generator)

    if context_generator == '' then
        context_generator = nil
    end

    local cfg = table.copy(log_cfg)
    cfg.context_generator = context_generator
    log_check_cfg(cfg)

    log_cfg.context_generator = context_generator

    box_cfg_update('context_generator')

    log_debug("log: context_generator set to '%s'", context_generator)
end

local function log_configure(self, cfg, box_api)
    if not box_api then
        if not log_initialized then
            local env_cfg = box.internal.env_cfg(box2log_keys)
            box.internal.apply_env_cfg(cfg, box_to_log_cfg(env_cfg))
        end
        cfg = box.internal.prepare_cfg(cfg, log_cfg, default_cfg, option_types)
    end

    log_check_cfg(cfg)
    local cfg_C = log_C_cfg(cfg)
    ffi.C.say_logger_init(cfg_C.log, cfg_C.level,
                          cfg_C.nonblock, cfg_C.format)
    log_initialized = true

    for o in pairs(option_types) do
        log_cfg[o] = cfg[o]
    end

    box_cfg_update()

    log_debug("log.cfg({log=%s, level=%s, nonblock=%s, format=%s, " ..
              "context_generator=%s, fields_order=%s})",
              cfg.log, cfg.level, cfg.nonblock, cfg.format,
              cfg.context_generator, json.encode(cfg.fields_order))
end

local compat_warning_said = false
local compat_v16 = {
    logger_pid = function()
        if not compat_warning_said then
            compat_warning_said = true
            log_warn('logger_pid() is deprecated, please use pid() instead')
        end
        return log_pid()
    end;
}

-- Log registry. It stores loggers, created by log_new, each of them having a
-- custom name. Allows to return same logger on consequent require('log') calls.
local log_registry = {}
-- Forward declaration.
local log_main = require('log')

-- Create a logger with a custom name.
local function log_new(name)
    if type(name) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS, 'name should be a string')
    end

    if log_registry[name] ~= nil then
        return log_registry[name]
    end

    local log = table.copy(log_main)
    log.name = name
    log.error = say_closure(log, S_ERROR)
    log.warn = say_closure(log, S_WARN)
    log.info = say_closure(log, S_INFO)
    log.verbose = say_closure(log, S_VERBOSE)
    log.debug = say_closure(log, S_DEBUG)
    log_registry[name] = log
    return log
end

-- Main logger, returned by the non-overloaded require('log')
log_main.warn = log_warn
log_main.info = log_info
log_main.verbose = log_verbose
log_main.debug = log_debug
log_main.error = log_error
log_main.new = log_new
log_main.rotate = log_rotate
log_main.pid = log_pid
log_main.level = set_log_level
log_main.log_format = set_log_format
log_main.log_fields_order = set_log_fields_order
log_main.log_context_generator = set_log_context_generator
log_main.cfg = setmetatable(log_cfg, {
    __call = function(self, cfg) log_configure(self, cfg, false) end,
})
log_main.box_api = {
    cfg = function()
        log_configure(log_cfg, box_to_log_cfg(box.cfg), true)
    end,
    cfg_check = function() log_check_cfg(box_to_log_cfg(box.cfg)) end,
}
log_main.internal = {
    ratelimit = {
        new = ratelimit_new,
        enable = ratelimit_enable,
        disable = ratelimit_disable,
    },
}

setmetatable(log_main, {
    __serialize = function(self)
        local res = table.copy(self)
        res.box_api = nil
        return setmetatable(res, {})
    end,
    __index = compat_v16;
})

return log_main
