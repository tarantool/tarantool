-- Logger wrapper that allows to enable debug logging for config's
-- code unconditionally using TT_CONFIG_DEBUG=1.
--
-- Hopefully, the wrapper can be dropped when TT_LOG_LEVEL starts
-- to work before log.cfg()/box.cfg() (gh-8092).

local logger_name = 'tarantool.config'
local log = require('log').new(logger_name)

local func2prefix = {
    [log.error] = 'E> ',
    [log.warn] = 'W> ',
    [log.info] = 'I> ',
    [log.verbose] = 'V> ',
    [log.debug] = 'D> ',
}

-- Accept false/true case insensitively.
--
-- Accept 0/1 as boolean values.
--
-- If anything else is given, just return the default.
local function boolean_fromenv(raw_value, default)
    if raw_value == nil or raw_value == '' then
        return default
    end
    if raw_value:lower() == 'false' or raw_value == '0' then
        return false
    end
    if raw_value:lower() == 'true' or raw_value == '1' then
        return true
    end
    return default
end

local function say_closure(log_f)
    local prefix = ''

    -- Enable logging of everything if an environment variable is
    -- set.
    --
    -- Useful for debugging.
    --
    -- Just setting of...
    --
    -- ```
    -- TT_LOG_MODULES='{"tarantool.config": "debug"}'
    -- ```
    --
    -- ...is not suitable due to gh-8092: messages before first
    -- box.cfg() are not shown.
    --
    -- Explicit calling of...
    --
    -- ```
    -- log.cfg({modules = {['tarantool.config'] = 'debug'}})
    -- ```
    --
    -- is not suitable as well, because it makes the logger
    -- already configured and non-dynamic options like
    -- `box_cfg.log_nonblock` can't be applied at first box.cfg()
    -- invocation.
    --
    -- So just prefix our log messages and use log.info().
    --
    -- The prefix represents the original log level of the
    -- message.
    if boolean_fromenv(os.getenv('TT_CONFIG_DEBUG'), false) then
        prefix = func2prefix[log_f]
        log_f = log.info
    end

    return function(fmt, ...)
        log_f(prefix .. fmt, ...)
    end
end

return {
    error = say_closure(log.error),
    warn = say_closure(log.warn),
    info = say_closure(log.info),
    verbose = say_closure(log.verbose),
    debug = say_closure(log.debug),
}
