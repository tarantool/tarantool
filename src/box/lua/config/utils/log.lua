-- Logger wrapper with a few enhancements.
--
-- 1. Encode tables into JSON.
-- 2. Enable all the messages on TT_CONFIG_DEBUG=1.
--
-- Hopefully, the wrapper can be dropped when the following logger
-- problems will be solved.
--
-- * JSONify tables at logging (gh-8611)
-- * Follow TT_LOG_LEVEL before log.cfg()/box.cfg() (gh-8092)
--
-- A solution of the following problems may simplify the code of
-- this wrapper.
--
-- * mylog.cfg() configures the main logger (gh-8610)
-- * No easy way to determine if a message should go to the log
--   (gh-8730)

local json_noexc = require('json').new()
json_noexc.cfg({encode_use_tostring = true})

local logger_name = 'tarantool.config'
local log = require('log').new(logger_name)

local func2level = {
    [log.error] = 2,
    [log.warn] = 3,
    [log.info] = 5,
    [log.verbose] = 6,
    [log.debug] = 7,
}

local func2prefix = {
    [log.error] = 'E> ',
    [log.warn] = 'W> ',
    [log.info] = 'I> ',
    [log.verbose] = 'V> ',
    [log.debug] = 'D> ',
}

local str2level = {
    ['fatal'] = 0,
    ['syserror'] = 1,
    ['error'] = 2,
    ['warn'] = 3,
    ['crit'] = 4,
    ['info'] = 5,
    ['verbose'] = 6,
    ['debug'] = 7,
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
        -- Skip logging based on the log level before performing
        -- the encoding into JSON.
        --
        -- log.new(<...>).cfg is the main logger configuration,
        -- the same as log.cfg. Extract the current log level from
        -- log.cfg.modules if it is set specifically for the config
        -- module. See gh-8610.
        local level = log.cfg.modules and
            log.cfg.modules[logger_name] or
            log.cfg.level
        -- The level is either a string or a number. Transform it
        -- to a number. See gh-8730.
        level = str2level[level] or level
        assert(type(level) == 'number')
        if func2level[log_f] > level then
            return
        end

        -- Micro-optimization: don't create a temporary table if
        -- it is not needed.
        local argc = select('#', ...)
        if argc == 0 then
            log_f(prefix .. fmt)
            return
        end

        -- Encode tables into JSON.
        --
        -- Ignores presence of __serialize and __tostring in the
        -- metatatable. It is suitable for the config module needs.
        local args = {...}
        for i = 1, argc do
            if type(args[i]) == 'table' then
                args[i] = json_noexc.encode(args[i])
            end
        end

        -- Pass the result to the logger function.
        log_f(prefix .. fmt, unpack(args, 1, argc))
    end
end

return {
    error = say_closure(log.error),
    warn = say_closure(log.warn),
    info = say_closure(log.info),
    verbose = say_closure(log.verbose),
    debug = say_closure(log.debug),
}
