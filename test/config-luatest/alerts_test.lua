local t = require('luatest')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

-- Ensure that added alerts are shown in box.info.config.alerts and can be
-- cleared.
g.test_alert_add_clear = function(g)
    local verify = function()
        local alerts = require('config'):new_alerts_namespace('my_alerts')
        t.assert_equals(#box.info.config.alerts, 0)

        local alert = {
            message = 'Test alert',
            my_field = 'my_value',
        }

        alerts:add(alert)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], alert)

        alerts:clear()
        t.assert_equals(#box.info.config.alerts, 0)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that set alerts are shown in box.info.config.alerts and if the same
-- key is used, the alert is replaced.
g.test_alert_set_reset_unset_clear = function(g)
    local verify = function()
        local alerts = require('config'):new_alerts_namespace('my_alerts')
        t.assert_equals(#box.info.config.alerts, 0)

        local key_1 = 'my_key_1'
        local key_2 = 'my_key_2'

        local alert_1 = {
            message = 'Test alert 1',
            my_field = 'my_value_1',
        }

        local alert_2 = {
            message = 'Test alert 2',
            my_field = 'my_value_2',
        }

        local alert_3 = {
            message = 'Test alert 3',
            my_field = 'my_value_3',
        }

        alerts:set(key_1, alert_1)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], alert_1)

        alerts:set(key_2, alert_2)
        t.assert_equals(#box.info.config.alerts, 2)
        t.assert_items_include(box.info.config.alerts[1], alert_1)
        t.assert_items_include(box.info.config.alerts[2], alert_2)

        alerts:add(alert_2)
        t.assert_equals(#box.info.config.alerts, 3)
        t.assert_items_include(box.info.config.alerts[1], alert_1)
        t.assert_items_include(box.info.config.alerts[2], alert_2)
        t.assert_items_include(box.info.config.alerts[3], alert_2)

        alerts:set(key_1, alert_3)
        t.assert_equals(#box.info.config.alerts, 3)
        t.assert_items_include(box.info.config.alerts[1], alert_2)
        t.assert_items_include(box.info.config.alerts[2], alert_2)
        t.assert_items_include(box.info.config.alerts[3], alert_3)

        alerts:unset(key_2)
        t.assert_equals(#box.info.config.alerts, 2)
        t.assert_items_include(box.info.config.alerts[1], alert_2)
        t.assert_items_include(box.info.config.alerts[2], alert_3)

        alerts:clear()
        t.assert_equals(#box.info.config.alerts, 0)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that multiple alerts can be added and are cleared correctly.
g.test_alert_multiple_add_clear = function(g)
    local verify = function()
        local alerts = require('config'):new_alerts_namespace('my_alerts')
        t.assert_equals(#box.info.config.alerts, 0)

        local alert_1 = {
            message = 'Test alert 1',
            my_field = 'my_value_1',
        }

        local alert_2 = {
            message = 'Test alert 2',
            my_field = 'my_value_2',
        }

        alerts:add(alert_1)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], alert_1)

        alerts:add(alert_2)
        t.assert_equals(#box.info.config.alerts, 2)
        t.assert_items_include(box.info.config.alerts[1], alert_1)
        t.assert_items_include(box.info.config.alerts[2], alert_2)

        alerts:clear()
        t.assert_equals(#box.info.config.alerts, 0)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that alerts are namespace-specific and are cleared correctly.
g.test_multiple_alerts_namespaces = function(g)
    local verify = function()
        local alerts_1 = require('config'):new_alerts_namespace('my_alerts_1')
        local alerts_2 = require('config'):new_alerts_namespace('my_alerts_2')
        t.assert_equals(#box.info.config.alerts, 0)

        local alert_1 = {
            message = 'Test alert_1',
            my_field = 'my_value_1',
        }
        local alert_2 = {
            message = 'Test alert 2',
            my_field = 'my_value_2',
        }

        alerts_1:add(alert_1)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], alert_1)

        alerts_2:add(alert_2)
        t.assert_equals(#box.info.config.alerts, 2)
        t.assert_items_include(box.info.config.alerts[1], alert_1)
        t.assert_items_include(box.info.config.alerts[2], alert_2)

        alerts_1:clear()
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], alert_2)

        alerts_2:clear()
        t.assert_equals(#box.info.config.alerts, 0)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure namespaces with the same name are treated as the same namespace.
g.test_alerts_namespaces_with_same_name = function(g)
    local verify = function()
        local alerts_1 = require('config'):new_alerts_namespace('my_alerts')
        local alerts_2 = require('config'):new_alerts_namespace('my_alerts')
        t.assert_equals(#box.info.config.alerts, 0)

        local alert = {
            message = 'Test alert',
            my_field = 'my_value',
        }

        alerts_1:add(alert)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], alert)

        alerts_2:clear()
        t.assert_equals(#box.info.config.alerts, 0)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that alerts from namespaces are cleared by the aboard:clean() method.
g.test_alerts_cleaned_by_aboard = function(g)
    local verify = function()
        local config = require('config')
        local alerts = config:new_alerts_namespace('my_alerts')
        t.assert_equals(#box.info.config.alerts, 0)

        local alert = {
            message = 'Test alert',
            my_field = 'my_value',
        }

        alerts:add(alert)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], alert)

        config._aboard:clean()
        t.assert_equals(#box.info.config.alerts, 0)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

g.test_alert_set_unset_clear_return_values = function(g)
    local verify = function()
        local alerts = require('config'):new_alerts_namespace('my_alerts')

        t.assert_is(alerts:set('key1', {message = 'msg1'}), true)
        t.assert_is(alerts:set('key1', {message = 'msg1'}), false)
        t.assert_is(alerts:set('key1', {message = 'msg2'}), true)

        t.assert_is(alerts:unset('key1'), true)
        t.assert_is(alerts:unset('key1'), false)

        t.assert_is(alerts:set('key2', {message = 'msg2'}), true)
        t.assert_is(alerts:clear(), true)
        t.assert_is(alerts:clear(), false)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that add returns true, and that namespace:get() returns the alert
-- by key or nil.
g.test_alert_add_return_value_and_get = function(g)
    local verify = function()
        local alerts = require('config'):new_alerts_namespace('my_alerts')

        t.assert_is(alerts:add({message = 'msg1'}), true)

        alerts:set('key1', {message = 'msg2', my_field = 'val'})
        local alert = alerts:get('key1')
        t.assert_equals(alert.message, 'msg2')
        t.assert_equals(alert.my_field, 'val')

        t.assert_is(alerts:get('nonexistent'), nil)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that aboard-level set, get, drop_if, and clean return bool
-- (or alert/nil for :get()).
g.test_aboard_set_get_drop_if_clean_return_values = function(g)
    local verify = function()
        local aboard = require('config')._aboard

        t.assert_is(aboard:set({type = 'warn', message = 'msg1'},
                               {key = 'key1'}), true)
        t.assert_is(aboard:set({type = 'warn', message = 'msg1'},
                               {key = 'key1'}), false)
        t.assert_is(aboard:set({type = 'warn', message = 'msg2'},
                               {key = 'key1'}), true)
        t.assert_is(aboard:set({type = 'warn', message = 'msg3'},
                               {key = 'key2'}), true)

        -- Different type, same message: should update
        t.assert_is(aboard:set({type = 'error', message = 'x'},
                               {key = 'k'}), true)
        t.assert_is(aboard:set({type = 'warn', message = 'x'},
                               {key = 'k'}), true)

        t.assert_equals(aboard:get('key1').message, 'msg2')
        t.assert_is(aboard:get('nonexistent'), nil)

        t.assert_is(aboard:drop('nonexistent'), false)
        t.assert_is(aboard:drop('key1'), true)
        t.assert_is(aboard:drop('key1'), false)

        t.assert_is(aboard:drop_if(function() return false end), false)
        t.assert_is(aboard:drop_if(function() return true end), true)
        t.assert_is(aboard:drop_if(function() return true end), false)

        aboard:set({type = 'warn', message = 'msg4'}, {key = 'key4'})
        t.assert_is(aboard:clean(), true)
        t.assert_is(aboard:clean(), false)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that namespace:get() returns a copy.
g.test_alert_get_returns_copy = function(g)
    local verify = function()
        local alerts = require('config'):new_alerts_namespace('my_alerts')

        alerts:set('key1', {message = 'msg1', my_field = 'val'})
        local alert = alerts:get('key1')
        alert.message = 'modified'
        alert.my_field = 'modified'

        local stored = alerts:get('key1')
        t.assert_equals(stored.message, 'msg1')
        t.assert_equals(stored.my_field, 'val')
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that all faults are caught and reported correctly.
g.test_alerts_assertions = function()
    local cases = {
        {
            script = [[
                local config = require('config')
                config.new_alerts_namespace('my_alerts')
            ]],
            err = 'Use config:new_alerts_namespace',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts.add({message = 'Test alert'})
            ]],
            err = 'Use alerts_namespace:add',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts.set("my_key", {message = 'Test alert'})
            ]],
            err = 'Use alerts_namespace:set',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts.unset("my_key")
            ]],
            err = 'Use alerts_namespace:unset',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts.clear()
            ]],
            err = 'Use alerts_namespace:clear',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts:add("Test alert")
            ]],
            err = 'alert must be a table',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts:add({message = 'Test alert', type = 'error'})
            ]],
            err = 'alert.type must be nil or "warn"',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace(123)
            ]],
            err = 'namespace name must be a string',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace("my:alerts")
            ]],
            err = 'namespace name cannot contain a colon',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts:set(123, {message = 'Test alert'})
            ]],
            err = 'alert key must be a string',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts:set("my:key", {message = 'Test alert'})
            ]],
            err = 'alert key cannot contain a colon',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts.get("my_key")
            ]],
            err = 'Use alerts_namespace:get',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts:unset(123)
            ]],
            err = 'alert key must be a string',
        },
        {
            script = [[
                local config = require('config')
                local alerts = config:new_alerts_namespace('my_alerts')
                alerts:unset("my:key")
            ]],
            err = 'alert key cannot contain a colon',
        },
    }

    for _, case in ipairs(cases) do
        helpers.failure_case({
            options = {['app.module'] = 'main'},
            script = case.script,
            exp_err = case.err,
        })
    end
end
