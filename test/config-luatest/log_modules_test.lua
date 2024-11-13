local fio = require('fio')
local digest = require('digest')
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

-- Execute the given function on the given server and collect logs
-- produced during its execution.
--
-- Comparing to <server object>:exec() it has a limited
-- functionality:
--
-- * no arguments passing
-- * no return value receiving
-- * no luatest upvalue autorebinding
-- * no check that there are no other upvalues
--
-- Though, it is enough for the test cases below.
--
-- It would be nice to integrate the log capturing functionality
-- right into <server object>:exec() in a future. It would also be
-- nice to support logging to stderr, not just file, because
-- luatest anyway captures it.
--
-- Unlike <server object>:grep_log() this function is bounded to
-- a particular given function and leans on unique log markers, so
-- it is suitable in a situation, when several testing scenarios
-- are producing similar output.
--
-- Also, it just feels more user-friendly.
local function log_capturing_exec(server, f)
    -- Generate markers to issue them before/after the execution
    -- of the provided function and look them up inside logs
    -- later.
    local start_marker = digest.urandom(40):hex()
    local end_marker = digest.urandom(40):hex()

    -- Execute the provided function (and issue the markers
    -- before/after it).
    local func_body = string.dump(f)
    server:exec(function(func_body, start_marker, end_marker)
        local log = require('log')
        local func = loadstring(func_body)
        log.info(start_marker)
        func()
        log.info(end_marker)
    end, {func_body, start_marker, end_marker})

    -- Determine a path to the log file of the given server.
    local file = fio.pathjoin(server.chdir, 'var/log', server.alias,
        'tarantool.log')

    -- Cut off a text between start and end markers: the output
    -- of the provided function.
    local raw_log = t.helpers.retrying({timeout = 60}, function()
        -- Read the log file.
        local fh = assert(fio.open(file, {'O_RDONLY'}))
        local data = fh:read()
        fh:close()

        -- Look up positions of the start and the end markers.
        --
        -- We need the last position of the start marker and the
        -- first position of the end marker.
        local _, s = data:find(('%s\n'):format(start_marker))
        assert(s ~= nil, ('no test start marker: %s'):format(start_marker))
        local e = data:find(('\n[^\n]+%s'):format(end_marker), s)
        assert(e ~= nil, ('no test end marker: %s'):format(end_marker))

        -- All the text between them is what we're searching for.
        return data:sub(s + 1, e - 1)
    end)

    -- Split per line.
    --
    -- Remove all the columns except the log level and the
    -- message.
    local res = {}
    for _, line in ipairs(raw_log:split('\n')) do
        table.insert(res, line:match('.> .*$'))
    end
    return res
end

-- A function that issues log messages using various loggers on
-- different levels.
local function issue_logs()
    local log = require('log')

    for _, logger_name in ipairs({
        'mfatal',
        'msyserror',
        'merror',
        'mcrit',
        'mwarn',
        'minfo',
        'mdefault',
        'mverbose',
        'mdebug',
    }) do
        local logger = log.new(logger_name)
        for _, level in ipairs({
            'error',
            'warn',
            'info',
            'verbose',
            'debug',
        }) do
            logger[level]('hello from logger %s', logger_name)
        end
    end
end

-- Verify an effect of the log.modules option and an effect of
-- removing it (after :reload(), without restart).
--
-- This scenario was broken before gh-10728.
g.test_basic = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        -- luatest.server doesn't provide an API to catch
        -- tarantool's stdout/stderr output. Feed the logs to a
        -- file to read them from the test case.
        :set_global_option('log.to', 'file')
        -- Define loggers with different levels.
        :set_global_option('log.modules', {
            mfatal = 'fatal',
            msyserror = 'syserror',
            merror = 'error',
            mcrit = 'crit',
            mwarn = 'warn',
            minfo = 'info',
            -- mdefault is unset deliberately to verify the
            -- default level.
            mverbose = 'verbose',
            mdebug = 'debug',
        })
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Each logger shows log messages of the corresponding levels.
    --
    -- For example, minfo messages are on error, warn, info
    -- levels, while verbose and debug messages are suppressed.
    local res = log_capturing_exec(cluster['i-001'], issue_logs)
    t.assert_equals(res, {
        'E> hello from logger merror',

        'E> hello from logger mcrit',

        'E> hello from logger mwarn',
        'W> hello from logger mwarn',

        'E> hello from logger minfo',
        'W> hello from logger minfo',
        'I> hello from logger minfo',

        'E> hello from logger mdefault',
        'W> hello from logger mdefault',
        'I> hello from logger mdefault',

        'E> hello from logger mverbose',
        'W> hello from logger mverbose',
        'I> hello from logger mverbose',
        'V> hello from logger mverbose',

        'E> hello from logger mdebug',
        'W> hello from logger mdebug',
        'I> hello from logger mdebug',
        'V> hello from logger mdebug',
        'D> hello from logger mdebug',
    })

    -- Remove the log.modules option, write and reload the new
    -- configuration.
    --
    -- The log.modules option removal had no effect before
    -- gh-10728.
    local config_2 = cbuilder:new(config)
        :set_global_option('log.modules', nil)
        :config()
    cluster:reload(config_2)

    -- Now all the loggers are working on the default (info)
    -- level.
    local res = log_capturing_exec(cluster['i-001'], issue_logs)
    t.assert_equals(res, {
        'E> hello from logger mfatal',
        'W> hello from logger mfatal',
        'I> hello from logger mfatal',

        'E> hello from logger msyserror',
        'W> hello from logger msyserror',
        'I> hello from logger msyserror',

        'E> hello from logger merror',
        'W> hello from logger merror',
        'I> hello from logger merror',

        'E> hello from logger mcrit',
        'W> hello from logger mcrit',
        'I> hello from logger mcrit',

        'E> hello from logger mwarn',
        'W> hello from logger mwarn',
        'I> hello from logger mwarn',

        'E> hello from logger minfo',
        'W> hello from logger minfo',
        'I> hello from logger minfo',

        'E> hello from logger mdefault',
        'W> hello from logger mdefault',
        'I> hello from logger mdefault',

        'E> hello from logger mverbose',
        'W> hello from logger mverbose',
        'I> hello from logger mverbose',

        'E> hello from logger mdebug',
        'W> hello from logger mdebug',
        'I> hello from logger mdebug',
    })
end
