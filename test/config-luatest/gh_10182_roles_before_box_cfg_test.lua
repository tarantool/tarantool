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

local preload_role = [[
    -- tags: preload
]] .. simple_role

tg.test_without_preload_tag = function(g)
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

tg.test_with_preload_tag = function(g)
    local verify = function()
        t.assert_equals(_G.on_load_status, 'unconfigured')
        t.assert_equals(_G.on_apply_status, 'running')
    end

    helpers.success_case(g, {
        roles = {
            one = preload_role
        },
        options = {
            ['roles'] = {'one'}
        },
        verify = verify,
    })
end
