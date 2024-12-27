-- tags: parallel

local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.test_enable_debug = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
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
