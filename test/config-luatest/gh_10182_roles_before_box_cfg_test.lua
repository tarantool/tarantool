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

local early_load_role = [[#!/usr/bin/env tarantool


    -- early_load: true
]] .. simple_role

tg.test_without_early_load_tag = function(g)
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

tg.test_with_early_load_tag = function(g)
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

tg.test_with_early_load_tag_added = function(g)
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
            ['roles'] = {'one'},
        },
        roles = {},
        roles_2 = {
            one = early_load_role
        },
        verify = function() end,
        verify_2 = function() end,
    })

    t.assert(g.server:grep_log(
        'Role "one" with the "early_load" tag was added to the config, it ' ..
        'cannot be loaded before the first box.cfg call',
        1024, {filename = g.server.chdir .. '/tarantool.log'}))
end
