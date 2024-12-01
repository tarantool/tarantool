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
g.test_role_validate_error = function()
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

    helpers.failure_case({
        roles = {one = one},
        options = {
            ['roles_cfg'] = {one = 1},
            ['roles'] = {'one'}
        },
        exp_err = 'Wrong config for role one: something wrong'
    })
end

-- Ensure that errors during role application are handled correctly.
g.test_role_apply_error = function()
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

    helpers.failure_case({
        roles = {one = one},
        options = {
            ['roles_cfg'] = {one = 1},
            ['roles'] = {'one'}
        },
        exp_err = 'Error applying role one: something wrong'
    })
end

-- Make sure an error is raised if not all methods are present.
g.test_role_no_method_error = function()
    local one = [[
        return {
            apply = function() end,
            stop = function() end,
        }
    ]]
    helpers.failure_case({
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
    helpers.failure_case({
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
    helpers.failure_case({
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

-- Make sure dependencies in roles works as intended.
g.test_role_dependencies_success = function(g)
    local one = [[
        local function apply()
            _G.foo = _G.foo .. '_one'
        end

        return {
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]

    local two = [[
        local function apply()
            _G.foo = _G.foo .. '_two'
        end

        return {
            dependencies = {'one'},
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]

    local three = [[
        local function apply()
            _G.foo = _G.foo .. '_three'
        end

        return {
            dependencies = {'four', 'two'},
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]

    local four = [[
        _G.foo = 'four'

        return {
            validate = function() end,
            apply = function() end,
            stop = function() end,
        }
    ]]

    local five = [[
        local function apply()
            _G.foo = _G.foo .. '_five'
        end

        return {
            validate = function() end,
            apply = apply,
            stop = function() end,
        }
    ]]

    local verify = function()
        t.assert_equals(_G.foo, 'four_one_two_three_five')
    end

    helpers.success_case(g, {
        roles = {one = one, two = two, three = three, four = four, five = five},
        options = {
            ['roles'] = {'four', 'three', 'two', 'one', 'five'}
        },
        verify = verify,
    })
end

g.test_role_dependencies_error_wrong_type = function()
    local one = [[
        return {
            dependencies = 'two',
            validate = function() end,
            apply = function() end,
            stop = function() end,
        }
    ]]

    helpers.failure_case({
        roles = {one = one},
        options = {
            ['roles'] = {'one'}
        },
        exp_err = 'Role "one" has field "dependencies" of type string, '..
                  'array-like table or nil expected'
    })
end

g.test_role_dependencies_error_no_role = function()
    local one = [[
        return {
            dependencies = {'two'},
            validate = function() end,
            apply = function() end,
            stop = function() end,
        }
    ]]

    helpers.failure_case({
        roles = {one = one},
        options = {
            ['roles'] = {'one'}
        },
        exp_err = 'Role "one" requires role "two", but the latter is not in ' ..
                  'the list of roles of the instance'
    })
end

g.test_role_dependencies_error_self = function()
    local one = [[
        return {
            dependencies = {'one'},
            validate = function() end,
            apply = function() end,
            stop = function() end,
        }
    ]]

    helpers.failure_case({
        roles = {one = one},
        options = {
            ['roles'] = {'one'}
        },
        exp_err = 'Circular dependency: role "one" depends on itself'
    })
end

g.test_role_dependencies_error_circular = function()
    local one = [[
        return {
            dependencies = {'two'},
            validate = function() end,
            apply = function() end,
            stop = function() end,
        }
    ]]

    local two = [[
        return {
            dependencies = {'one'},
            validate = function() end,
            apply = function() end,
            stop = function() end,
        }
    ]]

    helpers.failure_case({
        roles = {one = one, two = two},
        options = {
            ['roles'] = {'one', 'two'}
        },
        exp_err = 'Circular dependency: roles "two" and "one" depend on ' ..
                  'each other'
    })
end

g.test_role_dependencies_stop_required_role = function(g)
    local one = [[
        return {
            dependencies = {'two', 'three'},
            validate = function() end,
            apply = function() end,
            stop = function() end,
        }
    ]]

    local two = [[
        return {
            dependencies = {'three'},
            validate = function() end,
            apply = function() end,
            stop = function() end,
        }
    ]]

    local three = [[
        return {
            validate = function() end,
            apply = function() end,
            stop = function() end,
        }
    ]]

    helpers.reload_failure_case(g, {
        roles = {one = one, two = two, three = three},
        roles_2 = {one = one, three = three},
        options = {
            ['roles'] = {'one', 'two', 'three'}
        },
        options_2 = {
            ['roles'] = {'one', 'three'}
        },
        verify = function() end,
        exp_err = 'Role "two" cannot be stopped because role "one" ' ..
                  'depends on it'
    })

    helpers.reload_failure_case(g, {
        roles = {one = one, two = two, three = three},
        roles_2 = {one = one, two = two},
        options = {
            ['roles'] = {'one', 'two', 'three'}
        },
        options_2 = {
            ['roles'] = {'one', 'two'}
        },
        verify = function() end,
        exp_err = 'Role "three" cannot be stopped because roles ' ..
                  '"one", "two" depend on it'
    })
end

-- Make sure that role was started after config was fully loaded.
g.test_role_started_and_stopped_after_config_loaded = function(g)
    local one = string.dump(function()
        local function apply()
            local cfg = require('config')
            local state = rawget(_G, 'state')
            table.insert(state, {'start', cfg:info().status, cfg:get('roles')})
        end

        local function stop()
            local cfg = require('config')
            local state = rawget(_G, 'state')
            table.insert(state, {'stop', cfg:info().status, cfg:get('roles')})
        end

        rawset(_G, 'state', {})

        return {
            validate = function() end,
            apply = apply,
            stop = stop,
        }
    end)
    local verify = function()
        local exp = {{'start', 'startup_in_progress', {'one'}}}
        t.assert_equals(rawget(_G, 'state'), exp)
    end

    local verify_2 = function()
        local exp = {
            {'start', 'startup_in_progress', {'one'}},
            {'stop', 'reload_in_progress'},
        }
        t.assert_equals(rawget(_G, 'state'), exp)
    end

    helpers.reload_success_case(g, {
        roles = {one = one},
        options = {
            ['roles'] = {'one'}
        },
        options_2 = {},
        verify = verify,
        verify_2 = verify_2,
    })
end

-- Ensure that a descriptive error is raised if the given module
-- is not a table.
g.test_role_is_not_a_table = function()
    local myrole = string.dump(function()
        return 42
    end)

    helpers.failure_case({
        roles = {myrole = myrole},
        options = {
            ['roles'] = {'myrole'}
        },
        exp_err = 'Unable to use module myrole as a role: ' ..
            'expected table, got number',
    })
end

-- Ensure that on_event callback is called correctly.
g.test_role_on_event = function(g)
    local one = [[
        _G.foo = -1
        _G.is_ro = 'unknown'

        return {
            validate = function() end,
            apply = function()
                _G.foo = 0
            end,
            stop = function()
                _G.foo = -1
            end,
            on_event = function(_, key, value)
                assert(_G.foo >= 0)

                if(_G.foo == 0) then
                    assert(key == 'config.apply')
                else
                    assert(key == 'box.status')
                end

                _G.is_ro = value.is_ro
                _G.foo = _G.foo + 1
            end,
        }
    ]]

    local verify = function()
        t.assert_equals(_G.foo, 1)
        t.assert_equals(_G.is_ro, false)

        box.cfg({read_only = true})
        t.helpers.retrying({}, function()
            t.assert_equals(_G.foo, 2)
            t.assert_equals(_G.is_ro, true)
        end)

        box.cfg({read_only = false})
        t.helpers.retrying({}, function()
            t.assert_equals(_G.foo, 3)
            t.assert_equals(_G.is_ro, false)
        end)
    end

    helpers.success_case(g, {
        roles = {one = one},
        options = {
            ['roles'] = {'one'}
        },
        verify = verify,
    })
end

-- Ensure that on_event callback is called correctly after reload.
g.test_role_on_event_reload = function(g)
    local one = [[
        _G.foo = -1
        _G.is_ro = 'unknown'

        return {
            validate = function() end,
            apply = function()
                _G.foo = 0
            end,
            stop = function()
                _G.foo = -1
            end,
            on_event = function(_, _, value)
                assert(_G.foo >= 0)
                _G.is_ro = value.is_ro
                _G.foo = _G.foo + 1
            end,
        }
    ]]

    local verify = function()
        t.assert_equals(rawget(_G, 'foo'), nil)
        t.assert_equals(rawget(_G, 'is_ro'), nil)
    end

    local verify_2 = function()
        t.assert_equals(_G.foo, 1)
        t.assert_equals(_G.is_ro, false)

        box.cfg({read_only = true})
        t.helpers.retrying({}, function()
            t.assert_equals(_G.foo, 2)
            t.assert_equals(_G.is_ro, true)
        end)

        box.cfg({read_only = false})
        t.helpers.retrying({}, function()
            t.assert_equals(_G.foo, 3)
            t.assert_equals(_G.is_ro, false)
        end)
    end

    helpers.reload_success_case(g, {
        roles = {one = one},
        options = {},
        options_2 = {
            ['roles'] = {'one'},
        },
        verify = verify,
        verify_2 = verify_2,
    })
end

-- Ensure that on_event callbacks are called in the correct order.
g.test_role_on_event_order = function(g)
    local one = [[
        _G.foo = -1
        _G.is_ro = 'unknown'

        return {
            validate = function() end,
            apply = function()
                _G.foo = 0
            end,
            stop = function()
                _G.foo = -1
            end,
            on_event = function(_, _, value)
                assert(_G.foo >= 0)
                _G.is_ro = value.is_ro
                _G.foo = _G.foo + 1
            end,
        }
    ]]

    local two = [[
        _G.bar = -1

        return {
            dependencies = {'one'},
            validate = function() end,
            apply = function()
                _G.bar = 0
            end,
            stop = function()
                _G.bar = -1
            end,
            on_event = function()
                assert(_G.foo >= 0)
                assert(_G.bar >= 0)
                _G.bar = _G.foo * 2
            end,
        }
    ]]

    local verify = function()
        t.assert_equals(_G.foo, 1)
        t.assert_equals(_G.bar, 2)
        t.assert_equals(_G.is_ro, false)

        box.cfg({read_only = true})
        t.helpers.retrying({}, function()
            t.assert_equals(_G.foo, 2)
            t.assert_equals(_G.bar, 4)
            t.assert_equals(_G.is_ro, true)
        end)

        box.cfg({read_only = false})
        t.helpers.retrying({}, function()
            t.assert_equals(_G.foo, 3)
            t.assert_equals(_G.bar, 6)
            t.assert_equals(_G.is_ro, false)
        end)
    end

    helpers.success_case(g, {
        roles = {one = one, two = two},
        options = {
            ['roles'] = {'one', 'two'},
        },
        verify = verify,
    })
end

-- Ensure that on_event callback error is logged and doesn't prevent other
-- callbacks from being called.
g.test_role_on_event_error = function(g)
    local one = [[
        return {
            validate = function() end,
            apply = function() end,
            stop = function() end,
            on_event = function()
                error('something wrong')
            end,
        }
    ]]

    local two = [[
        _G.bar = -1

        return {
            dependencies = {'one'},
            validate = function() end,
            apply = function()
                _G.bar = 0
            end,
            stop = function()
                _G.bar = -1
            end,
            on_event = function()
                assert(_G.bar >= 0)
                _G.bar = _G.bar + 1
            end,
        }
    ]]

    local verify = function()
        t.assert_equals(_G.bar, 1)

        box.cfg({read_only = true})
        t.helpers.retrying({}, function()
            t.assert_equals(_G.bar, 2)
        end)

        box.cfg({read_only = false})
        t.helpers.retrying({}, function()
            t.assert_equals(_G.bar, 3)
        end)
    end

    helpers.success_case(g, {
        roles = {one = one, two = two},
        options = {
            ['roles'] = {'one', 'two'},
            ['log'] = {
                ['to'] = 'file',
                ['file'] = './var/log/instance-001/tarantool.log',
            },
        },
        verify = verify,
    })

    local log_postfix = '/var/log/instance-001/tarantool.log'
    t.assert(g.server:grep_log(
        'roles.on_event: callback for role "one" failed: ' ..
        '.* something wrong', 1024, {filename = g.server.chdir .. log_postfix}))
end
