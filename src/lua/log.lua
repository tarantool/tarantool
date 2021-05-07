-- log.lua
--
local ffi = require('ffi')
ffi.cdef[[
    typedef void (*sayfunc_t)(int level, const char *filename, int line,
               const char *error, const char *format, ...);

    enum say_logger_type {
        SAY_LOGGER_BOOT,
        SAY_LOGGER_STDERR,
        SAY_LOGGER_FILE,
        SAY_LOGGER_PIPE,
        SAY_LOGGER_SYSLOG
    };

    enum say_logger_type
    log_type();

    void
    say_set_log_level(int new_level);

    void
    say_set_log_format(enum say_format format);

    extern void
    say_logger_init(const char *init_str, int level, int nonblock,
                    const char *format, int background);

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

    int
    say_parse_logger_type(const char **str, enum say_logger_type *type);
]]

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

local function fmt_list()
    local keyset = {}
    for k in pairs(fmt_str2num) do
        keyset[#keyset + 1] = k
    end
    return table.concat(keyset, ',')
end

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
local log_cfg = {
    log             = nil,
    nonblock        = nil,
    level           = S_INFO,
    format          = fmt_num2str[ffi.C.SF_PLAIN],
}

-- Name mapping from box to log module and
-- back. Make sure all required fields
-- are covered!
local log2box_keys = {
    ['log']             = 'log',
    ['nonblock']        = 'log_nonblock',
    ['level']           = 'log_level',
    ['format']          = 'log_format',
}

local box2log_keys = {
    ['log']             = 'log',
    ['log_nonblock']    = 'nonblock',
    ['log_level']       = 'level',
    ['log_format']      = 'format',
}

-- Update cfg value(s) in box.cfg instance conditionally
local function box_cfg_update(log_key)
    -- if it is not yet even exist just exit early
    if type(box.cfg) ~= 'table' then
        return
    end

    local update = function(log_key, box_key)
        -- the box entry may be under configuration
        -- process thus equal to nil, skip it then
        if log_cfg[log_key] ~= nil and
            box.cfg[box_key] ~= nil and
            box.cfg[box_key] ~= log_cfg[log_key] then
            box.cfg[box_key] = log_cfg[log_key]
        end
    end

    if log_key == nil then
        for k, v in pairs(log2box_keys) do
            update(k, v)
        end
    else
        assert(log2box_keys[log_key] ~= nil)
        update(log_key, log2box_keys[log_key])
    end
end

-- Log options which can be set ony once.
local cfg_static_keys = {
    log         = true,
    nonblock    = true,
}

-- Test if static key is not changed.
local function verify_static(k, v)
    assert(cfg_static_keys[k] ~= nil)

    if ffi.C.say_logger_initialized() == true then
        if log_cfg[k] ~= v then
            return false, "can't be set dynamically"
        end
    end

    return true
end

local function parse_format(log)
    -- There is no easy way to get pointer to ponter via FFI
    local str_p = ffi.new('const char*[1]')
    str_p[0] = ffi.cast('char*', log)
    local logger_type = ffi.new('enum say_logger_type[1]')
    local rc = ffi.C.say_parse_logger_type(str_p, logger_type)

    if rc ~= 0 then
        box.error()
    end

    return logger_type[0]
end

-- Test if format is valid.
local function verify_format(key, name, cfg)
    assert(log_cfg[key] ~= nil)

    if not fmt_str2num[name] then
        local m = "expected %s"
        return false, m:format(fmt_list())
    end

    local log_type = ffi.C.log_type()

    -- When comes from log.cfg{} or box.cfg{}
    -- initial call we might be asked to setup
    -- syslog with json which is not allowed.
    --
    -- Note the cfg table comes from two places:
    -- box api interface and log module itself.
    -- The good thing that we're only needed log
    -- entry which is the same key for both.
    if cfg ~= nil and cfg['log'] ~= nil then
        log_type = parse_format(cfg['log'])
    end

    if fmt_str2num[name] == ffi.C.SF_JSON then
        if log_type == ffi.C.SAY_LOGGER_SYSLOG then
            local m = "%s can't be used with syslog logger"
            return false, m:format(fmt_num2str[ffi.C.SF_JSON])
        end
    end

    return true
end

-- Test if level is a valid string. The
-- number may be any for to backward compatibility.
local function verify_level(key, level)
    assert(log_cfg[key] ~= nil)

    if type(level) == 'string' then
        if not log_level_keys[level] then
            local m = "expected %s"
            return false, m:format(log_level_list())
        end
    elseif type(level) ~= 'number' then
            return false, "must be a number or a string"
    end

    return true
end

local verify_ops = {
    ['log']         = verify_static,
    ['nonblock']    = verify_static,
    ['format']      = verify_format,
    ['level']       = verify_level,
}

-- Verify a value for the particular key.
local function verify_option(k, v, ...)
    assert(k ~= nil)

    if verify_ops[k] ~= nil then
        return verify_ops[k](k, v, ...)
    end

    return true
end

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

-- Rotate log (basically reopen the log file and
-- start writting into it).
local function log_rotate()
    ffi.C.say_logrotate(nil, nil, 0)
end

-- Set new logging level, the level must be valid!
local function set_log_level(level, update_box_cfg)
    assert(type(level) == 'number')

    ffi.C.say_set_log_level(level)

    rawset(log_cfg, 'level', level)

    if update_box_cfg then
        box_cfg_update('level')
    end

    local m = "log: level set to %s"
    say(S_DEBUG, m:format(level))
end

-- Tries to set a new level, or print an error.
local function log_level(level)
    local ok, msg = verify_option('level', level)
    if not ok then
        error(msg)
    end

    if type(level) == 'string' then
        level = log_level_keys[level]
    end

    set_log_level(level, true)
end

-- Set a new logging format, the name must be valid!
local function set_log_format(name, update_box_cfg)
    assert(fmt_str2num[name] ~= nil)

    if fmt_str2num[name] == ffi.C.SF_JSON then
        ffi.C.say_set_log_format(ffi.C.SF_JSON)
    else
        ffi.C.say_set_log_format(ffi.C.SF_PLAIN)
    end

    rawset(log_cfg, 'format', name)

    if update_box_cfg then
        box_cfg_update('format')
    end

    local m = "log: format set to '%s'"
    say(S_DEBUG, m:format(name))
end

-- Tries to set a new format, or print an error.
local function log_format(name)
    local ok, msg = verify_option('format', name)
    if not ok then
        error(msg)
    end

    set_log_format(name, true)
end

-- Returns pid of a pipe process.
local function log_pid()
    return tonumber(ffi.C.log_pid)
end

-- Fetch a value from log to box.cfg{}.
local function box_api_cfg_get(key)
    return log_cfg[box2log_keys[key]]
end

-- Set value to log from box.cfg{}.
local function box_api_cfg_set(cfg, key, value)
    local log_key = box2log_keys[key]

    -- a special case where we need to restore
    -- nil value from previous setup attempt.
    if value == box.NULL then
        log_cfg[log_key] = nil
        return true
    end

    local ok, msg = verify_option(log_key, value, cfg)
    if not ok then
        return false, msg
    end

    log_cfg[log_key] = value
    return true
end

-- Set logging level from reloading box.cfg{}
local function box_api_cfg_set_log_level()
    local log_key = box2log_keys['log_level']
    local v = box.cfg['log_level']

    local ok, msg = verify_option(log_key, v)
    if not ok then
        return false, msg
    end

    if type(v) == 'string' then
        v = log_level_keys[v]
    end

    set_log_level(v, false)
    return true
end

-- Set logging format from reloading box.cfg{}
local function box_api_set_log_format()
    local log_key = box2log_keys['log_format']
    local v = box.cfg['log_format']

    local ok, msg = verify_option(log_key, v)
    if not ok then
        return false, msg
    end

    set_log_format(v, false)
    return true
end

-- Reload dynamic options.
local function reload_cfg(cfg)
    for k in pairs(cfg_static_keys) do
        if cfg[k] ~= nil then
            local ok, msg = verify_static(k, cfg[k])
            if not ok then
                local m = "log.cfg: \'%s\' %s"
                error(m:format(k, msg))
            end
        end
    end

    if cfg.level ~= nil then
        log_level(cfg.level)
    end

    if cfg.format ~= nil then
        log_format(cfg.format)
    end
end

-- Load or reload configuration via log.cfg({}) call.
local function load_cfg(self, cfg)
    cfg = cfg or {}

    -- log option might be zero length string, which
    -- is fine, we should treat it as nil.
    if cfg.log ~= nil then
        if type(cfg.log) ~= 'string' or cfg.log:len() == 0 then
            cfg.log = nil
        end
    end

    if cfg.format ~= nil then
        local ok, msg = verify_option('format', cfg.format, cfg)
        if not ok then
            local m = "log.cfg: \'%s\' %s"
            error(m:format('format', msg))
        end
    end

    if cfg.level ~= nil then
        local ok, msg = verify_option('level', cfg.level)
        if not ok then
            local m = "log.cfg: \'%s\' %s"
            error(m:format('level', msg))
        end
        -- Convert level to a numeric value since
        -- low level api operates with numbers only.
        if type(cfg.level) == 'string' then
            assert(log_level_keys[cfg.level] ~= nil)
            cfg.level = log_level_keys[cfg.level]
        end
    end

    if cfg.nonblock ~= nil then
        if type(cfg.nonblock) ~= 'boolean' then
            error("log.cfg: 'nonblock' option must be 'true' or 'false'")
        end
    end

    if ffi.C.say_logger_initialized() == true then
        return reload_cfg(cfg)
    end

    cfg.level = cfg.level or log_cfg.level
    cfg.format = cfg.format or log_cfg.format
    cfg.nonblock = cfg.nonblock or log_cfg.nonblock

    -- nonblock is special: it has to become integer
    -- for ffi call but in config we have to save
    -- true value only for backward compatibility!
    local nonblock = cfg.nonblock

    if nonblock == nil or nonblock == false then
        nonblock = 0
    else
        nonblock = 1
    end

    -- Parsing for validation purposes
    if cfg.log ~= nil then
        parse_format(cfg.log)
    end

    -- We never allow confgure the logger in background
    -- mode since we don't know how the box will be configured
    -- later.
    ffi.C.say_logger_init(cfg.log, cfg.level,
                          nonblock, cfg.format, 0)

    if nonblock == 1 then
        nonblock = true
    else
        nonblock = nil
    end

    -- Update log_cfg vars to show them in module
    -- configuration output.
    rawset(log_cfg, 'log', cfg.log)
    rawset(log_cfg, 'level', cfg.level)
    rawset(log_cfg, 'nonblock', nonblock)
    rawset(log_cfg, 'format', cfg.format)

    -- and box.cfg output as well.
    box_cfg_update()

    local m = "log.cfg({log=%s,level=%s,nonblock=%s,format=\'%s\'})"
    say(S_DEBUG, m:format(cfg.log, cfg.level, cfg.nonblock, cfg.format))
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
    warn = say_closure(S_WARN),
    info = say_closure(S_INFO),
    verbose = say_closure(S_VERBOSE),
    debug = say_closure(S_DEBUG),
    error = say_closure(S_ERROR),
    rotate = log_rotate,
    pid = log_pid,
    level = log_level,
    log_format = log_format,
    cfg = setmetatable(log_cfg, {
        __call = load_cfg,
    }),
    box_api = {
        cfg_get = box_api_cfg_get,
        cfg_set = box_api_cfg_set,
        cfg_set_log_level = box_api_cfg_set_log_level,
        cfg_set_log_format = box_api_set_log_format,
    },
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
