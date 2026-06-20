-- tags: parallel

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

        t.assert_equals(alerts:count(), 1)
        t.assert_covers(alerts:alerts()[1], alert)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert)

        alerts:clear()
        t.assert_equals(alerts:count(), 0)
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
        t.assert_equals(alerts:count(), 0)
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
        t.assert_covers(alerts:get(key_1), alert_1)
        t.assert_equals(alerts:count(), 1)
        t.assert_covers(alerts:alerts()[key_1], alert_1)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert_1)

        -- Check that allert doesn't contain private fields.
        for k, _ in pairs(alerts:get(key_1)) do
            t.assert_not_equals(k[1], '_')
        end
        for k, _ in pairs(alerts:alerts()[key_1]) do
            t.assert_not_equals(k[1], '_')
        end
        for k, _ in pairs(box.info.config.alerts[1]) do
            t.assert_not_equals(k[1], '_')
        end

        alerts:set(key_2, alert_2)
        t.assert_covers(alerts:get(key_1), alert_1)
        t.assert_covers(alerts:get(key_2), alert_2)
        t.assert_equals(alerts:count(), 2)
        t.assert_covers(alerts:alerts()[key_1], alert_1)
        t.assert_covers(alerts:alerts()[key_2], alert_2)
        t.assert_equals(#box.info.config.alerts, 2)
        t.assert_covers(box.info.config.alerts[1], alert_1)
        t.assert_covers(box.info.config.alerts[2], alert_2)

        alerts:add(alert_2)
        t.assert_covers(alerts:get(key_1), alert_1)
        t.assert_covers(alerts:get(key_2), alert_2)
        t.assert_equals(alerts:count(), 3)
        t.assert_covers(alerts:alerts()[key_1], alert_1)
        t.assert_covers(alerts:alerts()[key_2], alert_2)
        t.assert_covers(alerts:alerts()[1], alert_2)
        t.assert_equals(#box.info.config.alerts, 3)
        t.assert_covers(box.info.config.alerts[1], alert_1)
        t.assert_covers(box.info.config.alerts[2], alert_2)
        t.assert_covers(box.info.config.alerts[3], alert_2)

        alerts:set(key_1, alert_3)
        t.assert_covers(alerts:get(key_1), alert_3)
        t.assert_covers(alerts:get(key_2), alert_2)
        t.assert_equals(alerts:count(), 3)
        t.assert_covers(alerts:alerts()[key_1], alert_3)
        t.assert_covers(alerts:alerts()[key_2], alert_2)
        t.assert_covers(alerts:alerts()[1], alert_2)
        t.assert_equals(#box.info.config.alerts, 3)
        t.assert_covers(box.info.config.alerts[1], alert_2)
        t.assert_covers(box.info.config.alerts[2], alert_2)
        t.assert_covers(box.info.config.alerts[3], alert_3)

        alerts:unset(key_2)
        t.assert_covers(alerts:get(key_1), alert_3)
        t.assert_equals(alerts:get(key_2), nil)
        t.assert_equals(alerts:count(), 2)
        t.assert_covers(alerts:alerts()[key_1], alert_3)
        t.assert_covers(alerts:alerts()[1], alert_2)
        t.assert_equals(#box.info.config.alerts, 2)
        t.assert_covers(box.info.config.alerts[1], alert_2)
        t.assert_covers(box.info.config.alerts[2], alert_3)

        alerts:clear()
        t.assert_equals(alerts:count(), 0)
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
        t.assert_equals(alerts:count(), 0)
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
        t.assert_equals(alerts:count(), 1)
        t.assert_covers(alerts:alerts()[1], alert_1)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert_1)

        alerts:add(alert_2)
        t.assert_equals(alerts:count(), 2)
        t.assert_covers(alerts:alerts()[1], alert_1)
        t.assert_covers(alerts:alerts()[2], alert_2)
        t.assert_equals(#box.info.config.alerts, 2)
        t.assert_covers(box.info.config.alerts[1], alert_1)
        t.assert_covers(box.info.config.alerts[2], alert_2)

        alerts:clear()
        t.assert_equals(alerts:count(), 0)
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
        t.assert_equals(alerts_1:count(), 0)
        t.assert_equals(alerts_2:count(), 0)
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
        t.assert_equals(alerts_1:count(), 1)
        t.assert_equals(alerts_2:count(), 0)
        t.assert_covers(alerts_1:alerts()[1], alert_1)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert_1)

        alerts_2:add(alert_2)
        t.assert_equals(alerts_1:count(), 1)
        t.assert_equals(alerts_2:count(), 1)
        t.assert_covers(alerts_1:alerts()[1], alert_1)
        t.assert_covers(alerts_2:alerts()[1], alert_2)
        t.assert_equals(#box.info.config.alerts, 2)
        t.assert_covers(box.info.config.alerts[1], alert_1)
        t.assert_covers(box.info.config.alerts[2], alert_2)

        alerts_1:clear()
        t.assert_equals(alerts_1:count(), 0)
        t.assert_equals(alerts_2:count(), 1)
        t.assert_covers(alerts_2:alerts()[1], alert_2)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert_2)

        alerts_2:clear()
        t.assert_equals(alerts_1:count(), 0)
        t.assert_equals(alerts_2:count(), 0)
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
        t.assert_equals(alerts_1:count(), 0)
        t.assert_equals(alerts_2:count(), 0)
        t.assert_equals(#box.info.config.alerts, 0)

        local alert = {
            message = 'Test alert',
            my_field = 'my_value',
        }

        alerts_1:add(alert)
        t.assert_equals(alerts_1:count(), 1)
        t.assert_equals(alerts_2:count(), 1)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(alerts_1:alerts()[1], alert)
        t.assert_covers(alerts_2:alerts()[1], alert)
        t.assert_covers(box.info.config.alerts[1], alert)

        alerts_2:clear()
        t.assert_equals(alerts_1:count(), 0)
        t.assert_equals(alerts_2:count(), 0)
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
        t.assert_equals(alerts:count(), 0)
        t.assert_equals(#box.info.config.alerts, 0)

        local alert = {
            message = 'Test alert',
            my_field = 'my_value',
        }

        alerts:add(alert)
        t.assert_equals(alerts:count(), 1)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(alerts:alerts()[1], alert)
        t.assert_covers(box.info.config.alerts[1], alert)

        config._aboard:clean()
        t.assert_equals(alerts:count(), 0)
        t.assert_equals(#box.info.config.alerts, 0)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that alerts returned by get and alerts methods are copies.
-- Modifying them does not affect the stored alerts.
g.test_alerts_return_copies = function(g)
    local verify = function()
        local config = require('config')
        local alerts = config:new_alerts_namespace('my_alerts')
        t.assert_equals(alerts:count(), 0)
        t.assert_equals(#box.info.config.alerts, 0)

        local key = 'my_key'
        local alert = {
            message = 'Test alert',
            my_field = 'my_value',
        }

        alerts:set(key, alert)
        t.assert_covers(alerts:get(key), alert)
        t.assert_equals(alerts:count(), 1)
        t.assert_covers(alerts:alerts()[key], alert)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert)

        -- Modify the returned alert from get method.
        local got_alert = alerts:get(key)
        got_alert.message = 'Modified message'
        got_alert.new_field = 'new_value'

        -- Check that the stored alert is not affected.
        t.assert_covers(alerts:get(key), alert)
        t.assert_equals(alerts:count(), 1)
        t.assert_covers(alerts:alerts()[key], alert)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert)

        -- Modify the returned alerts from alerts method.
        local got_alerts = alerts:alerts()
        got_alerts[key].message = 'Modified message'
        got_alerts[key].new_field = 'new_value'

        -- Check that the stored alert is not affected.
        t.assert_covers(alerts:get(key), alert)
        t.assert_equals(alerts:count(), 1)
        t.assert_covers(alerts:alerts()[key], alert)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that system namespace provides get and alerts methods.
g.test_system_alerts_api = function(g)
    local verify = function()
        local config = require('config')
        local system_alerts = config:new_alerts_namespace()

        -- Check that the system alerts namespace object
        -- has only get, alerts and count methods.
        local system_alerts_mt = getmetatable(system_alerts)
        local method_count = 0
        for _, _ in pairs(system_alerts_mt.__index) do
            method_count = method_count + 1
        end
        t.assert_equals(method_count, 3)
        t.assert(system_alerts_mt.__index.get)
        t.assert(system_alerts_mt.__index.alerts)
        t.assert(system_alerts_mt.__index.count)

        t.assert_equals(system_alerts:count(), 0)
        t.assert_equals(#box.info.config.alerts, 0)

        local key = 'my_key'
        local alert_1 = {
            message = 'Test alert 1',
            type = 'warn',
            my_field = 'my_value_1',
        }
        local alert_2 = {
            message = 'Test alert 2',
            type = 'error',
            my_field = 'my_value_2',
        }

        config._aboard:set(alert_1, {key = key})
        t.assert_covers(system_alerts:get(key), alert_1)
        t.assert_equals(system_alerts:count(), 1)
        t.assert_covers(system_alerts:alerts()[key], alert_1)
        t.assert_equals(#box.info.config.alerts, 1)
        -- Check that allert doesn't contain private fields.
        for k, _ in pairs(system_alerts:get(key)) do
            t.assert_not_equals(k[1], '_')
        end

        config._aboard:set(alert_2)
        t.assert_covers(system_alerts:get(key), alert_1)
        t.assert_equals(system_alerts:count(), 2)
        t.assert_covers(system_alerts:alerts()[key], alert_1)
        t.assert_covers(system_alerts:alerts()[1], alert_2)
        t.assert_equals(#box.info.config.alerts, 2)


        config._aboard:clean()
        t.assert_equals(system_alerts:get(key), nil)
        t.assert_equals(system_alerts:count(), 0)
        t.assert_equals(#box.info.config.alerts, 0)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that system alerts returned by get and alerts methods are copies.
-- Modifying them does not affect the stored alerts.
g.test_system_alerts_return_copies = function(g)
    local verify = function()
        local config = require('config')
        local system_alerts = config:new_alerts_namespace()
        t.assert_equals(system_alerts:count(), 0)
        t.assert_equals(#box.info.config.alerts, 0)

        local key = 'my_key'
        local alert = {
            message = 'Test alert',
            type = 'warn',
            my_field = 'my_value',
        }

        config._aboard:set(alert, {key = key})
        t.assert_covers(system_alerts:get(key), alert)
        t.assert_equals(system_alerts:count(), 1)
        t.assert_covers(system_alerts:alerts()[key], alert)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert)

        -- Modify the returned alert from get method.
        local got_alert = system_alerts:get(key)
        got_alert.message = 'Modified message'
        got_alert.new_field = 'new_value'

        -- Check that the stored alert is not affected.
        t.assert_covers(system_alerts:get(key), alert)
        t.assert_equals(system_alerts:count(), 1)
        t.assert_covers(system_alerts:alerts()[key], alert)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert)

        -- Modify the returned alerts from alerts method.
        local got_alerts = system_alerts:alerts()
        got_alerts[key].message = 'Modified message'
        got_alerts[key].new_field = 'new_value'

        -- Check that the stored alert is not affected.
        t.assert_covers(system_alerts:get(key), alert)
        t.assert_equals(system_alerts:count(), 1)
        t.assert_covers(system_alerts:alerts()[key], alert)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_covers(box.info.config.alerts[1], alert)
    end

    helpers.success_case(g, {
        verify = verify,
    })
end

-- Ensure that all faults are caught and reported correctly.
local errorneous_cases = {
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
            alerts.set('my_key', {message = 'Test alert'})
        ]],
        err = 'Use alerts_namespace:set',
    },
    {
        script = [[
            local config = require('config')
            local alerts = config:new_alerts_namespace('my_alerts')
            alerts.unset('my_key')
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
            alerts.get('my_key')
        ]],
        err = 'Use alerts_namespace:get',
    },
    {
        script = [[
            local config = require('config')
            local alerts = config:new_alerts_namespace('my_alerts')
            alerts.alerts()
        ]],
        err = 'Use alerts_namespace:alerts',
    },
    {
        script = [[
            local config = require('config')
            local alerts = config:new_alerts_namespace('my_alerts')
            alerts.count()
        ]],
        err = 'Use alerts_namespace:count',
    },
    {
        script = [[
            local config = require('config')
            local alerts = config:new_alerts_namespace('my_alerts')
            alerts:add('Test alert')
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
            local alerts = config:new_alerts_namespace('my:alerts')
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
            alerts:set('my:key', {message = 'Test alert'})
        ]],
        err = 'alert key cannot contain a colon',
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
            alerts:unset('my:key')
        ]],
        err = 'alert key cannot contain a colon',
    },
    {
        script = [[
            local config = require('config')
            local alerts = config:new_alerts_namespace('my_alerts')
            alerts:get(123)
        ]],
        err = 'alert key must be a string',
    },
    {
        script = [[
            local config = require('config')
            local alerts = config:new_alerts_namespace()
            alerts.get('my_key')
        ]],
        err = 'Use alerts_namespace:get',
    },
    {
        script = [[
            local config = require('config')
            local alerts = config:new_alerts_namespace()
            alerts.alerts()
        ]],
        err = 'Use alerts_namespace:alerts',
    },
    {
        script = [[
            local config = require('config')
            local alerts = config:new_alerts_namespace()
            alerts.count()
        ]],
        err = 'Use alerts_namespace:count',
    },
    {
        script = [[
            local config = require('config')
            local alerts = config:new_alerts_namespace()
            alerts:get(123)
        ]],
        err = 'alert key must be a string',
    },
}

for i, case in ipairs(errorneous_cases) do
    local test_name = 'test_alerts_assertions_' .. tostring(i)
    g[test_name] = function()
        helpers.failure_case({
            options = {['app.module'] = 'main'},
            script = case.script,
            exp_err = case.err,
        })
    end
end
