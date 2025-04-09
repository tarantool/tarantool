local t = require('luatest')
local helpers = require('test.config-luatest.helpers')

---@class luatest.group
local tg = helpers.group()

local simple_role = [[
    _G.on_load_status = nil
    _G.on_apply_status = nil

    local function on_load()
        _G.on_load_status = box.info.status
    end

    local function apply()
        _G.on_apply_status = box.info.status
    end

    on_load()

    return {
        validate = function() end,
        apply = apply,
        stop = function() end,
    }
]]

local early_load_role = [[
    -- --- #tarantool.metadata.v1
    -- early_load: true
    -- ...
]] .. simple_role

local simple_script = [[
    if (rawget(_G, 'on_load_status') == nil) then
        _G.on_load_status = box.info.status
    end
]]

local early_load_script = [[#!/usr/bin/env tarantool
    -- --- #tarantool.metadata.v1
    -- early_load: true
    -- ...
]] .. simple_script

tg.test_role_without_early_load_tag = function(g)
    local verify = function()
        t.assert_equals(_G.on_load_status, 'running')
        t.assert_equals(_G.on_apply_status, 'running')
    end

    helpers.success_case(g, {
        roles = {
            one = simple_role
        },
        options = {
            ['roles'] = {'one'}
        },
        verify = verify,
    })
end

tg.test_role_with_early_load_tag = function(g)
    local verify = function()
        t.assert_equals(_G.on_load_status, 'unconfigured')
        t.assert_equals(_G.on_apply_status, 'running')
    end

    helpers.success_case(g, {
        roles = {
            one = early_load_role
        },
        options = {
            ['roles'] = {'one'}
        },
        verify = verify,
    })
end

tg.test_role_with_early_load_tag_added = function(g)
    helpers.reload_success_case(g, {
        options = {
            ['log'] = {
                ['to'] = 'file',
                ['file'] = 'tarantool.log',
            },
            ['roles'] = {'one'},
        },
        options_2 = {
            ['log'] = {
                ['to'] = 'file',
                ['file'] = 'tarantool.log',
            },
            ['roles'] = {'one', 'two'},
        },
        roles = {
            one = early_load_role,
        },
        roles_2 = {
            one = early_load_role,
            two = early_load_role,
        },
        verify = function() end,
        verify_2 = function() end,
    })

    t.assert_not(g.server:grep_log(
        'Role "one" with the "early_load" tag was added to the config, it ' ..
        'cannot be loaded before the first box.cfg call',
        1024, {filename = g.server.chdir .. '/tarantool.log'}))

    t.assert(g.server:grep_log(
        'Role "two" with the "early_load" tag was added to the config, it ' ..
        'cannot be loaded before the first box.cfg call',
        1024, {filename = g.server.chdir .. '/tarantool.log'}))
end

tg.test_app_without_early_load_tag = function(g)
    local verify = function()
        t.assert_equals(_G.on_load_status, 'running')
    end

    helpers.success_case(g, {
        script = simple_script,
        options = {
               ['app.file'] = 'main.lua',
           },
        verify = verify,
    })

    helpers.success_case(g, {
        script = simple_script,
        options = {
               ['app.module'] = 'main',
           },
        verify = verify,
    })
end

tg.test_app_with_early_load_tag = function(g)

    local verify = function()
        t.assert_equals(_G.on_load_status, 'unconfigured')
    end

    helpers.success_case(g, {
        script = early_load_script,
        options = {
               ['app.file'] = 'main.lua',
           },
        verify = verify,
    })

    helpers.success_case(g, {
        script = early_load_script,
        options = {
               ['app.module'] = 'main',
           },
        verify = verify,
    })
end

tg.test_app_with_early_load_tag_added = function(g)
    helpers.reload_success_case(g, {
        options = {
            ['log'] = {
                ['to'] = 'file',
                ['file'] = 'tarantool.log',
            },
        },
        options_2 = {
            ['log'] = {
                ['to'] = 'file',
                ['file'] = 'tarantool.log',
            },
            ['app.file'] = 'main.lua',
        },
        verify = function() end,
        verify_2 = function() end,
        script_2 = early_load_script,
    })

    t.assert(g.server:grep_log(
        'App "main.lua" with the "early_load" tag was added to the config, ' ..
        'it cannot be loaded before the first box.cfg call',
        1024, {filename = g.server.chdir .. '/tarantool.log'}))

    helpers.reload_success_case(g, {
        options = {
            ['log'] = {
                ['to'] = 'file',
                ['file'] = 'tarantool.log',
            },
        },
        options_2 = {
            ['log'] = {
                ['to'] = 'file',
                ['file'] = 'tarantool.log',
            },
            ['app.module'] = 'main',
        },
        verify = function() end,
        verify_2 = function() end,
        script_2 = early_load_script,
    })

    t.assert(g.server:grep_log(
        'App "main" with the "early_load" tag was added to the config, ' ..
        'it cannot be loaded before the first box.cfg call',
        1024, {filename = g.server.chdir .. '/tarantool.log'}))


    helpers.reload_success_case(g, {
        options = {
            ['log'] = {
                ['to'] = 'file',
                ['file'] = 'tarantool.log',
            },
            ['app.file'] = 'main.lua',
        },
        options_2 = {
            ['log'] = {
                ['to'] = 'file',
                ['file'] = 'tarantool.log',
            },
            ['app.file'] = 'main.lua',
        },
        verify = function() end,
        verify_2 = function() end,
        script = simple_script,
        script_2 = early_load_script,
    })

    t.assert(g.server:grep_log(
        'App "main.lua" with the "early_load" tag was added to the config, ' ..
        'it cannot be loaded before the first box.cfg call',
        1024, {filename = g.server.chdir .. '/tarantool.log'}))

    helpers.reload_success_case(g, {
        options = {
            ['log'] = {
                ['to'] = 'file',
                ['file'] = 'tarantool.log',
            },
            ['app.module'] = 'main',
        },
        options_2 = {
            ['log'] = {
                ['to'] = 'file',
                ['file'] = 'tarantool.log',
            },
            ['app.module'] = 'main',
        },
        verify = function() end,
        verify_2 = function() end,
        script = simple_script,
        script_2 = early_load_script,
    })

    t.assert_not(g.server:grep_log(
        'App "main" with the "early_load" tag was added to the config, ' ..
        'it cannot be loaded before the first box.cfg call',
        1024, {filename = g.server.chdir .. '/tarantool.log'}))
end
