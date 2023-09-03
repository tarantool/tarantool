local t = require('luatest')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

-- Make sure the role is properly loaded.
g.test_single_role_success = function(g)
    local one = [[
        local function apply(cfg)
            _G.bar = cfg
        end

        _G.foo = 42
        _G.bar = nil

        return {
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]
    local verify = function()
        local config = require('config')
        t.assert_equals(_G.foo, 42)
        local roles_cfg = config:get('roles_cfg')
        t.assert_equals(roles_cfg['one'], 12345)
        t.assert_equals(roles_cfg['one'], _G.bar)
    end

    helpers.success_case(g, {
        roles = {one = one},
        options = {
            ['roles_cfg'] = {one = 12345},
            ['roles'] = {'one'}
        },
        verify = verify,
    })
end

-- Make sure the role is loaded only once during each run.
g.test_role_repeat_success = function(g)
    local one = [[
        local function apply(cfg)
            _G.foo = _G.foo * cfg
        end

        _G.foo = 42

        return {
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]
    local verify = function()
        local config = require('config')
        local roles_cfg = config:get('roles_cfg')
        t.assert_equals(roles_cfg['one'], 3)
        t.assert_equals(_G.foo, 42 * 3)
    end

    helpers.success_case(g, {
        roles = {one = one},
        options = {
            ['roles_cfg'] = {one = 3},
            ['roles'] = {'one', 'one', 'one', 'one', 'one', 'one', 'one'}
        },
        verify = verify,
    })
end

-- Make sure all roles are loaded correctly and in correct order.
g.test_multiple_role_success = function(g)
    local one = [[
        local function apply(cfg)
            _G.foo = tonumber(cfg)
        end

        _G.foo = nil

        return {
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]

    local two = [[
        local function apply(cfg)
            _G.foo = _G.foo + tonumber(cfg)
        end

        return {
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]

    local three = [[
        local function apply(cfg)
            _G.foo = foo / tonumber(cfg)
        end

        return {
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]

    local verify = function()
        local config = require('config')
        t.assert_equals(config:get('roles'), {'one', 'two', 'three'})
        local roles_cfg = config:get('roles_cfg')
        local one = roles_cfg['one']
        local two = roles_cfg['two']
        local three = roles_cfg['three']
        t.assert_equals(one, 42)
        t.assert_equals(two, '24')
        t.assert_equals(three, 3)
        t.assert_equals(_G.foo, 22)
    end

    helpers.success_case(g, {
        roles = {one = one, two = two, three = three},
        options = {
            ['roles_cfg'] = {one = 42, two = '24', three = 3},
            ['roles'] = {'one', 'two', 'three'}
        },
        verify = verify,
    })
end

-- Make sure the roles call apply() during a reload.
g.test_role_reload_success = function(g)
    local one = [[
        local function apply(cfg)
            _G.foo = _G.foo * cfg
        end

        _G.foo = 42

        return {
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]

    local verify = function()
        local config = require('config')
        local roles_cfg = config:get('roles_cfg')
        t.assert_equals(roles_cfg['one'], 2)
        t.assert_equals(_G.foo, 42 * 2)
    end

    local verify_2 = function()
        local config = require('config')
        local roles_cfg = config:get('roles_cfg')
        t.assert_equals(roles_cfg['one'], 3)
        t.assert_equals(_G.foo, 42 * 2 * 3)
    end

    helpers.reload_success_case(g, {
        roles = {one = one},
        roles_2 = {one = one},
        options = {
            ['roles_cfg'] = {one = 2},
            ['roles'] = {'one'}
        },
        options_2 = {
            ['roles_cfg'] = {one = 3},
            ['roles'] = {'one'}
        },
        verify = verify,
        verify_2 = verify_2,
    })
end

-- Make sure the roles are stopped after reload if they were removed from the
-- configuration.
g.test_role_stop_success = function(g)
    local one = [[
        local function apply(cfg)
            _G.foo = _G.foo * cfg
        end

        local function stop()
            _G.foo = _G.foo - 100
        end

        _G.foo = 42

        return {
            validate = function() end,
            apply = apply,
            stop = stop,
        }
    ]]

    local verify = function()
        local config = require('config')
        t.assert_equals(config:get('roles'), {'one'})
        local roles_cfg = config:get('roles_cfg')
        t.assert_equals(roles_cfg['one'], 2)
        t.assert_equals(_G.foo, 42 * 2)
    end

    local verify_2 = function()
        local config = require('config')
        t.assert_equals(config:get('roles'), nil)
        local roles_cfg = config:get('roles_cfg')
        t.assert_equals(roles_cfg['one'], 1)
        t.assert_equals(_G.foo, 42 * 2 - 100)
    end

    helpers.reload_success_case(g, {
        roles = {one = one},
        roles_2 = {one = one},
        options = {
            ['roles_cfg'] = {one = 2},
            ['roles'] = {'one'}
        },
        options_2 = {
            ['roles_cfg'] = {one = 1},
        },
        verify = verify,
        verify_2 = verify_2,
    })
end

-- Ensure that errors during config validation are handled correctly.
g.test_role_validate_error = function(g)
    local one = [[
        local function validate(cfg)
            error('something wrong', 0)
        end

        return {
            validate = validate,
            apply = function() end,
            stop = function() end,
        }
    ]]

    helpers.failure_case(g, {
        roles = {one = one},
        options = {
            ['roles_cfg'] = {one = 1},
            ['roles'] = {'one'}
        },
        exp_err = 'Wrong config for role one: something wrong'
    })
end

-- Ensure that errors during role application are handled correctly.
g.test_role_apply_error = function(g)
    local one = [[
        local function apply(cfg)
            error('something wrong', 0)
        end

        return {
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]

    helpers.failure_case(g, {
        roles = {one = one},
        options = {
            ['roles_cfg'] = {one = 1},
            ['roles'] = {'one'}
        },
        exp_err = 'Error applying role one: something wrong'
    })
end

-- Make sure an error is raised if not all methods are present.
g.test_role_no_method_error = function(g)
    local one = [[
        return {
            apply = function() end,
            stop = function() end,
        }
    ]]
    helpers.failure_case(g, {
        roles = {one = one},
        options = {
            ['roles_cfg'] = {one = 1},
            ['roles'] = {'one'}
        },
        exp_err = 'Role one does not contain function validate'
    })

    one = [[
        return {
            validate = function() end,
            stop = function() end,
        }
    ]]
    helpers.failure_case(g, {
        roles = {one = one},
        options = {
            ['roles_cfg'] = {one = 1},
            ['roles'] = {'one'}
        },
        exp_err = 'Role one does not contain function apply'
    })

    one = [[
        return {
            validate = function() end,
            apply = function() end,
        }
    ]]
    helpers.failure_case(g, {
        roles = {one = one},
        options = {
            ['roles_cfg'] = {one = 1},
            ['roles'] = {'one'}
        },
        exp_err = 'Role one does not contain function stop'
    })
end

-- Ensure that errors during role stopping are handled correctly.
--
-- Also verify that the error is raised by config:reload() and the same error
-- appears in the alerts.
g.test_role_reload_error = function(g)
    local one = [[
        return {
            validate = function() end,
            apply = function() end,
            stop = function() error('Wrongly stopped', 0) end,
        }
    ]]

    helpers.reload_failure_case(g, {
        roles = {one = one},
        roles_2 = {one = one},
        options = {
            ['roles_cfg'] = {one = 1},
            ['roles'] = {'one'}
        },
        options_2 = {
            ['roles_cfg'] = {one = 1},
        },
        verify = function() end,
        exp_err = 'Error stopping role one: Wrongly stopped'
    })
end
