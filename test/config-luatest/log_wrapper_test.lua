local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local g = t.group()

g.before_all(function(g)
    treegen.init(g)
end)

g.after_all(function(g)
    treegen.clean(g)
end)

g.test_jsonify_table = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'main.lua', [[
        local log = require('internal.config.utils.log')

        log.info('foo: %s', {bar = 'baz'})
    ]])
    local env = {}
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, env, {'main.lua'}, opts)
    t.assert_equals(res, {
        exit_code = 0,
        stdout = '',
        stderr = 'foo: {"bar":"baz"}',
        env = os.environ(),
    })
end

-- {{{ Follow log level

local log_levels = {
    'default',
    0, 'fatal',
    1, 'syserror',
    2, 'error',
    3, 'crit',
    4, 'warn',
    5, 'info',
    6, 'verbose',
    7, 'debug',
}

local logger_funcs = {
    'error',
    'warn',
    'info',
    'verbose',
    'debug',
}

local str2level = setmetatable({
    ['default']  = 5,
    ['fatal']    = 0,
    ['syserror'] = 1,
    ['error']    = 2,
    ['crit']     = 3,
    ['warn']     = 4,
    ['info']     = 5,
    ['verbose']  = 6,
    ['debug']    = 7,
}, {
    __call = function(self, x)
        x = self[x] or x
        assert(type(x) == 'number')
        return x
    end,
})

local script = [[
    local main_log = require('log')
    local log = require('internal.config.utils.log')

    local current_level = tonumber(arg[1]) or arg[1]
    local msg_level = arg[2]
    local set_module_logger_level = arg[3] == 'true'

    if current_level ~= 'default' then
        if set_module_logger_level then
            main_log.cfg({
                modules = {
                    ['tarantool.config'] = current_level,
                },
            })
        else
            main_log.cfg({level = current_level})
        end
    end

    log[msg_level]('foo')
]]

for _, current_level in ipairs(log_levels) do
    for _, msg_level in ipairs(logger_funcs) do
        for _, set_module_logger_level in pairs({false, true}) do
            local case_name = ('test_follow_log_level_%s_%s_%s'):format(
                current_level, msg_level, set_module_logger_level)
            g[case_name] = function(g)
                local dir = treegen.prepare_directory(g, {}, {})
                treegen.write_script(dir, 'main.lua', script)
                local opts = {nojson = true, stderr = true}

                local args = {'main.lua', tostring(current_level), msg_level,
                    tostring(set_module_logger_level)}
                local res = justrun.tarantool(dir, {}, args, opts)
                t.assert_equals(res.exit_code, 0, res)

                if str2level(msg_level) <= str2level(current_level) then
                    t.assert(res.stderr:find('foo') ~= nil)
                else
                    t.assert(res.stderr:find('foo') == nil)
                end
            end
        end
    end
end

-- }}} Follow log level

g.test_enable_debug = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'main.lua', [[
        local log = require('internal.config.utils.log')

        -- A single string argument.
        log.debug('debug')
        log.verbose('verbose')
        log.info('info')
        log.warn('warn')
        log.error('error')

        -- A format string plus arguments.
        log.debug('debug %s', {1})
        log.verbose('verbose %s', {1})
        log.info('info %s', {1})
        log.warn('warn %s', {1})
        log.error('error %s', {1})
    ]])
    local env = {['TT_CONFIG_DEBUG'] = '1'}
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, env, {'main.lua'}, opts)
    t.assert_equals(res, {
        exit_code = 0,
        stdout = '',
        stderr = table.concat({
            'D> debug',
            'V> verbose',
            'I> info',
            'W> warn',
            'E> error',
            'D> debug [1]',
            'V> verbose [1]',
            'I> info [1]',
            'W> warn [1]',
            'E> error [1]',
        }, '\n'),
    })
end
