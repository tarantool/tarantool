-- log.lua
--
local fiber = require('fiber')
local ffi = require('ffi')
ffi.cdef[[
    typedef void (*sayfunc_t)(int level, const char *filename, int line,
               const char *error, const char *format, ...);

    struct say_module {
        const char *name;
        int log_level;
    };

    void
    say_set_log_level(int new_level);

    int
    say_get_log_level(void);

    void
    say_set_log_format(enum say_format format);

    void
    say_set_modules(const struct say_module *modules);

    int
    say_get_module_log_level(const char *name);

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
    say_logrotate();

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

    extern int log_level_flightrec;
    extern void
    log_write_flightrec_from_lua(int level, const char *filename, int line,
                                 ...);
]]

local S_WARN = ffi.C.S_WARN
local S_INFO = ffi.C.S_INFO
local S_VERBOSE = ffi.C.S_VERBOSE
local S_DEBUG = ffi.C.S_DEBUG
local S_ERROR = ffi.C.S_ERROR

---Tarantool provides a set of options used to configure logging in various
---ways: you can set a level of logging, specify where to send the log's
---output, configure a log format, and so on. The `log` module allows you to
---configure logging in your application and provides additional
---capabilities, for example, logging custom messages and rotating log
---files.
local log = {}

---@class log.cfg_options
---@field log? string where to send the log's output, for example, to a
---    file, pipe, or system logger.
---@field nonblock? boolean if true, Tarantool does not block during logging
---    when the system is not ready for writing, and drops the message
---    instead.
---@field level? number|string the level of detail the log has.
---@field format? string the log format: 'plain' or 'json'.
---@field modules? table<string, string|number> configure the specified log
---    levels for different modules.

---@class log.cfg_object: log.cfg_options
---@field __call fun(cfg?: log.cfg_options): nil

---@alias log.write_fn (fun(message: any)) & (fun(fmt: string, ...: any))

---@class log
---@field warn log.write_fn Log a message with the warn level.
---@field info log.write_fn Log a message with the info level.
---@field error log.write_fn Log a message with the error level.
---@field verbose log.write_fn Log a message with the verbose level.
---@field debug log.write_fn Log a message with the debug level.
---@field level fun(level: number|string) Set the log level.
---@field new fun(name: string): log Create a new logger with the specified
---    name.
---@field rotate fun() Rotate the log. For example, you need to call this
---    function to continue logging after a log rotation program renames or
---    moves a file with the latest logs.
---@field pid fun(): number? Return a PID of a logger. You can use this PID to
---    send a signal to a log rotation program, so it can rotate logs.
---@field log_format fun(format: string) Set the log format.
---@field cfg log.cfg_object Configure logging options. For
---    example, to send the log to a file, a pipe, or a system logger:
---    ```lua
---    log.cfg { log = 'tarantool.log' }
---    log.cfg { log = '| cronolog tarantool.log' }
---    log.cfg { log = 'syslog:server=unix:/dev/log' }
---    ```
---    To set the log level:
---    ```lua
---    log.cfg { level = 'verbose' }
---    ```

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
-- user API, so change with caution.
local default_cfg = {
    log             = nil,
    nonblock        = nil,
    level           = S_INFO,
    modules         = nil,
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
    ['modules']         = 'log_modules',
    ['format']          = 'log_format',
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

-- Main routine which pass data to C logging code.
local function say(self, level, fmt, ...)
    local name = self and self.name
    local module_level = name and ffi.C.say_get_module_log_level(name) or
                                  ffi.C.say_get_log_level()
    if level > log_normalize_level(module_level) and
       level > ffi.C.log_level_flightrec then
        return
    end
    local type_fmt = type(fmt)
    local format = "%s"
    local msg
    local argc = select('#', ...)
    if argc ~= 0 then
        local args = {...}
        for i = 1, argc do
            if type(args[i]) == 'table' then
                local mt = getmetatable(args[i])
                if mt == nil or mt.__tostring == nil then
                    args[i] = json.encode(args[i])
                end
            end
        end
        local stat
        stat, msg = pcall(string.format, fmt, unpack(args, 1, argc))
        if not stat then
            error(msg, 3)
        end
    elseif type_fmt == 'table' then
        msg = table.copy(fmt)
        if ffi.C.log_format == ffi.C.SF_JSON then
            -- use serialization function from the metatable, if any
            local msg_mt = getmetatable(msg)
            if msg_mt and type(msg_mt.__serialize) == 'function' then
                msg = msg_mt.__serialize(msg)
                if type(msg) ~= 'table' then
                    msg = { message = tostring(msg) }
                end
            end
            -- ignore internal keys
            for _, field in ipairs(special_fields) do
                msg[field] = nil
            end
            -- return empty string for an empty table
            if next(msg) == nil then
                msg.message = ''
            end
            -- set 'message' field if it is absent
            if msg.message == nil then
                msg.message = msg[1]
                msg[1] = nil
            end
            -- always encode tables as maps
            ---@cast msg table
            setmetatable(msg, json.map_mt)
            -- indicate that message is already encoded in JSON
            format = fmt_num2str[ffi.C.SF_JSON]
        end
        msg = json.encode(msg)
    else
        msg = tostring(fmt)
    end

    local debug = require('debug')
    local frame = debug.getinfo(3, "Sl")
    local line, file = 0, 'eval'
    if type(frame) == 'table' then
        line = frame.currentline or 0
        file = frame.short_src or frame.src or 'eval'
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

---Log a message with the error level.
---
---* A message can be a string.
---* A message may contain C-style format specifiers `%d` or `%s`. Example:
---* A message may be a scalar data type or a table. Example:
---
---The actual output will be a line in the log, containing:
---* The current timestamp
---* A module name
---* 'E', 'W', 'I', 'V' or 'D' depending on the called function.
---* `message`.
---
---**Example:**
---
--- ```lua
--- local log = require('log')
--- log.cfg { level = 'verbose' }
--- log.warn('Warning message')
--- log.info('Tarantool version: %s', box.info.version)
--- log.error({ 500, 'Internal error' })
--- log.debug('Debug message')
--- ```
---
---Note that the message will not be logged if the severity level
---corresponding to the called function is less than `log.level`.
---
---@overload fun(message: any)
---@param fmt string
---@param ... any
function log.error(fmt, ...)
    say(nil, S_ERROR, fmt, ...)
end
---Log a message with the warn level.
---
---* A message can be a string.
---* A message may contain C-style format specifiers `%d` or `%s`. Example:
---* A message may be a scalar data type or a table. Example:
---
---The actual output will be a line in the log, containing:
---* The current timestamp
---* A module name
---* 'E', 'W', 'I', 'V' or 'D' depending on the called function.
---* `message`.
---
---**Example:**
---
--- ```lua
--- local log = require('log')
--- log.cfg { level = 'verbose' }
--- log.warn('Warning message')
--- log.info('Tarantool version: %s', box.info.version)
--- log.error({ 500, 'Internal error' })
--- log.debug('Debug message')
--- ```
---
---Note that the message will not be logged if the severity level
---corresponding to the called function is less than `log.level`.
---
---@overload fun(message: any)
---@param fmt string
---@param ... any
function log.warn(fmt, ...)
    say(nil, S_WARN, fmt, ...)
end
---Log a message with the info level.
---
---* A message can be a string.
---* A message may contain C-style format specifiers `%d` or `%s`. Example:
---* A message may be a scalar data type or a table. Example:
---
---The actual output will be a line in the log, containing:
---* The current timestamp
---* A module name
---* 'E', 'W', 'I', 'V' or 'D' depending on the called function.
---* `message`.
---
---**Example:**
---
--- ```lua
--- local log = require('log')
--- log.cfg { level = 'verbose' }
--- log.warn('Warning message')
--- log.info('Tarantool version: %s', box.info.version)
--- log.error({ 500, 'Internal error' })
--- log.debug('Debug message')
--- ```
---
---Note that the message will not be logged if the severity level
---corresponding to the called function is less than `log.level`.
---
---@overload fun(message: any)
---@param fmt string
---@param ... any
function log.info(fmt, ...)
    say(nil, S_INFO, fmt, ...)
end
---Log a message with the verbose level.
---
---* A message can be a string.
---* A message may contain C-style format specifiers `%d` or `%s`. Example:
---* A message may be a scalar data type or a table. Example:
---
---The actual output will be a line in the log, containing:
---* The current timestamp
---* A module name
---* 'E', 'W', 'I', 'V' or 'D' depending on the called function.
---* `message`.
---
---**Example:**
---
--- ```lua
--- local log = require('log')
--- log.cfg { level = 'verbose' }
--- log.warn('Warning message')
--- log.info('Tarantool version: %s', box.info.version)
--- log.error({ 500, 'Internal error' })
--- log.debug('Debug message')
--- ```
---
---Note that the message will not be logged if the severity level
---corresponding to the called function is less than `log.level`.
---
---@overload fun(message: any)
---@param fmt string
---@param ... any
function log.verbose(fmt, ...)
    say(nil, S_VERBOSE, fmt, ...)
end
---Log a message with the debug level.
---
---* A message can be a string.
---* A message may contain C-style format specifiers `%d` or `%s`. Example:
---* A message may be a scalar data type or a table. Example:
---
---The actual output will be a line in the log, containing:
---* The current timestamp
---* A module name
---* 'E', 'W', 'I', 'V' or 'D' depending on the called function.
---* `message`.
---
---**Example:**
---
--- ```lua
--- local log = require('log')
--- log.cfg { level = 'verbose' }
--- log.warn('Warning message')
--- log.info('Tarantool version: %s', box.info.version)
--- log.error({ 500, 'Internal error' })
--- log.debug('Debug message')
--- ```
---
---Note that the message will not be logged if the severity level
---corresponding to the called function is less than `log.level`.
---
---@overload fun(message: any)
---@param fmt string
---@param ... any
function log.debug(fmt, ...)
    say(nil, S_DEBUG, fmt, ...)
end

-- Rotate log (basically reopen the log file and
-- start writting into it).
---Rotate the log.
---
---For example, you need to call this function to continue logging after a
---log rotation program renames or moves a file with the latest logs.
---
---@return nil
local function log_rotate()
    ffi.C.say_logrotate()
end

---Return a PID of a logger.
---
---You can use this PID to send a signal to a log rotation program, so it
---can rotate logs.
---
---@return number? pid
local function log_pid()
    return tonumber(ffi.C.log_pid)
end

local option_types = {
    log = 'string',
    nonblock = 'boolean',
    level = 'number, string',
    modules = 'table',
    format = 'string',
}

local log_initialized = false

-- Convert cfg options to types suitable for ffi say_ functions.
local function log_C_cfg(cfg)
    local cfg_C = table.copy(cfg)
    cfg_C.level = log_normalize_level(cfg.level)
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

---Set log level.
---
---@param level number|string the new log level, either a number or a string
---    such as 'verbose'.
local function set_log_level(level)
    box.internal.check_cfg_option_type(option_types.level, 'level', level)
    local cfg = table.copy(log_cfg)
    cfg.level = level
    log_check_cfg(cfg)

    local cfg_C = log_C_cfg(cfg)
    ffi.C.say_set_log_level(cfg_C.level)
    log_cfg.level = level

    box_cfg_update('level')

    log.debug("log: level set to %s", level)
end

---Set log format.
---
---@param format string the new log format: 'plain' or 'json'.
local function set_log_format(format)
    box.internal.check_cfg_option_type(option_types.format, 'format', format)
    local cfg = table.copy(log_cfg)
    cfg.format = format
    log_check_cfg(cfg)

    ffi.C.say_set_log_format(fmt_str2num[format])
    log_cfg.format = format

    box_cfg_update('format')

    log.debug("log: format set to '%s'", format)
end

local function set_log_modules(modules)
    modules = modules or {}
    local count = 0
    for _ in pairs(modules) do
        count = count + 1
    end
    local cfg = ffi.new('struct say_module[?]', count + 1)
    local i = 0
    for name, log_level in pairs(modules) do
        cfg[i].name = name
        cfg[i].log_level = log_normalize_level(log_level)
        i = i + 1
    end
    cfg[count].name = nil
    cfg[count].log_level = 0
    ffi.C.say_set_modules(cfg)
end

---Configure logging options.
---
---The following options are available:
---
---* `level`: specify the level of detail the log has.
---* `log`: specify where to send the log's output, for example, to a file,
---  pipe, or system logger.
---* `nonblock`: if true, Tarantool does not block during logging when the
---  system is not ready for writing, and drops the message instead.
---* `format`: specify the log format: 'plain' or 'json'.
---* `modules`: configure the specified log levels for different modules.
---
---@param cfg log.cfg_options the logging options.
local function log_configure(self, cfg, box_api)
    if not box_api then
        if cfg == nil then
            cfg = {}
        elseif type(cfg) ~= 'table' then
            error("Error: cfg should be a table")
        end
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
    set_log_modules(cfg.modules)
    log_initialized = true

    for o in pairs(option_types) do
        log_cfg[o] = cfg[o]
    end

    box_cfg_update()

    log.debug("log.cfg({log=%s, level=%s, nonblock=%s, format=%s})",
              cfg.log, cfg.level, cfg.nonblock, cfg.format)
end

local compat_warning_said = false
local compat_v16 = {
    logger_pid = function()
        if not compat_warning_said then
            compat_warning_said = true
            log.warn('logger_pid() is deprecated, please use pid() instead')
        end
        return log_pid()
    end;
}

-- Log registry. It stores loggers, created by log.new, each of them having a
-- custom name. Allows to return same logger on consequent require('log') calls.
local log_registry = {}

-- Create a logger with a custom name.
---Create a new logger with the specified name.
---
---*Since 2.11.0*
---
---You can configure a specific log level for a new logger using the
---`log.modules` configuration property.
---
---**Example:**
---
---```lua
---local module1_log = require('log').new('module1')
---local module2_log = require('log').new('module2')
---module1_log.verbose('module1 message')
---module2_log.error('module2 message')
---```
---
---@param name string a logger name.
---@return log logger a logger instance.
function log.new(name)
    if type(name) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS, 'name should be a string')
    end

    if log_registry[name] ~= nil then
        return log_registry[name]
    end

    local logger = table.copy(log)
    logger.name = name
    logger.error = say_closure(logger, S_ERROR)
    logger.warn = say_closure(logger, S_WARN)
    logger.info = say_closure(logger, S_INFO)
    logger.verbose = say_closure(logger, S_VERBOSE)
    logger.debug = say_closure(logger, S_DEBUG)
    log_registry[name] = logger
    return logger
end

if fiber._internal.cord_is_main then
    ---Rotate the log. For example, you need to call this function to
    ---continue logging after a log rotation program renames or moves a
    ---file with the latest logs.
    ---@return nil
    function log.rotate()
        log_rotate()
    end
    ---Return a PID of a logger. You can use this PID to send a signal to
    ---a log rotation program, so it can rotate logs.
    ---@return number? pid
    function log.pid()
        return log_pid()
    end
    ---Set the log level.
    ---@param level number|string the new log level, either a number or a
    ---    string such as 'verbose'.
    function log.level(level)
        set_log_level(level)
    end
    ---Set the log format.
    ---@param format string the new log format: 'plain' or 'json'.
    function log.log_format(format)
        set_log_format(format)
    end
    ---Configure logging options. For example, to send the log to a file,
    ---a pipe or a system logger:
    ---```lua
    ---log.cfg { log = 'file.log' }
    ---log.cfg { log = '| cronolog file.log' }
    ---log.cfg { log = 'syslog:server=unix:/dev/log' }
    ---```
    ---To set the log level:
    ---```lua
    ---log.cfg { level = 'verbose' }
    ---```
    log.cfg = setmetatable(log_cfg, {
        __call = function(self, cfg) log_configure(self, cfg, false) end,
    })
    log.box_api = {
        cfg = function()
            log_configure(log_cfg, box_to_log_cfg(box.cfg), true)
        end,
        cfg_check = function() log_check_cfg(box_to_log_cfg(box.cfg)) end,
    }
end

setmetatable(log, {
    __serialize = function(self)
        local res = table.copy(self)
        res.box_api = nil
        return setmetatable(res, {})
    end,
    __index = compat_v16;
})

return log
