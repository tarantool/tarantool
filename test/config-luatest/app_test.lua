local t = require('luatest')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

-- Start a script that is pointed by app.file or app.module.
--
-- Verify that the script has access to config:get() values.
g.test_startup_success = function(g)
    local script = [[
        local config = require('config')
        assert(config:get('app.cfg.foo') == 42)
        local info = config:info()
        assert(info.status == 'startup_in_progress')
        assert(#info.alerts == 0)

        _G.foo = 42
    ]]
    local verify = function()
        local config = require('config')
        t.assert_equals(_G.foo, 42)
        t.assert_equals(config:get('app.cfg.foo'), 42)
    end

    helpers.success_case(g, {
        script = script,
        options = {
            ['app.file'] = 'main.lua',
            ['app.cfg'] = {foo = 42},
        },
        verify = verify,
    })

    helpers.success_case(g, {
        script = script,
        options = {
            ['app.module'] = 'main',
            ['app.cfg'] = {foo = 42},
        },
        verify = verify,
    })
end

-- Start a script that is pointed by app.file or app.module.
--
-- Verify that an error in the script leads to a startup failure.
g.test_startup_error = function()
    local err_msg = 'Application is unable to start'
    local script = ([[
        error('%s', 0)
    ]]):format(err_msg)

    helpers.failure_case({
        script = script,
        options = {['app.file'] = 'main.lua'},
        exp_err = err_msg,
    })

    helpers.failure_case({
        script = script,
        options = {['app.module'] = 'main'},
        exp_err = err_msg,
    })
end

-- Start a server, write a script that raises an error, reload.
--
-- Verify that the error is raised by config:reload() and the same
-- error appears in the alerts.
--
-- The test case uses only app.file deliberately: if the
-- application script is set as app.module, it is not re-executed
-- on config:reload() due to caching in package.loaded.
g.test_reload_failure = function(g)
    local err_msg = 'Application is unable to start'

    helpers.reload_failure_case(g, {
        script = '',
        options = {['app.file'] = 'main.lua'},
        verify = function() end,
        script_2 = ([[
            error('%s', 0)
        ]]):format(err_msg),
        exp_err = err_msg,
    })
end

-- Start a server, write a script that raises an error, reload.
--
-- Verify that the error is NOT raised when the application script
-- is set using app.module (due to caching in package.loaded).
--
-- This behavior may be changed in a future.
g.test_reload_success = function(g)
    local err_msg = 'Application is unable to start'

    helpers.reload_success_case(g, {
        script = '',
        options = {['app.module'] = 'main'},
        verify = function() end,
        script_2 = ([[
            error('%s', 0)
        ]]):format(err_msg),
        verify_2 = function()
            local config = require('config')
            local info = config:info()
            t.assert_equals(info.status, 'ready')
            t.assert_equals(#info.alerts, 0)
        end,
    })
end

-- Verify that an application configuration is deeply merged for
-- tables with string keys.
--
-- This is supported after gh-10450.
g.test_app_cfg_deep_merge = function(g)
    local paths = {
        global = 'app.cfg',
        group = 'groups.group-001.app.cfg',
        replicaset = 'groups.group-001.replicasets.replicaset-001.app.cfg',
        instance = 'groups.group-001.replicasets.replicaset-001.instances.' ..
            'instance-001.app.cfg'
    }

    helpers.success_case(g, {
        options = {
            [paths.global]     = {x = {y = {z = {a = 1}}}},
            [paths.group]      = {x = {y = {z = {b = 2}}}},
            [paths.replicaset] = {x = {y = {z = {c = 3}}}},
            [paths.instance]   = {x = {y = {z = {d = 4}}}},
        },
        verify = function()
            local config = require('config')

            local res = config:get('app.cfg.x.y.z')
            t.assert_equals(res, {a = 1, b = 2, c = 3, d = 4})
        end,
    })
end
