local t = require('luatest')
local fio = require('fio')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group('checks')

g.after_each(function(g)
    if g.cluster ~= nil then
        g.cluster:stop()
    end
    if g.temp_dir ~= nil then
        fio.rmtree(g.temp_dir)
    end
end)

-- {{{ checks: off

g.test_checks_off_is_valid = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :set_instance_option('i-001', 'config.checks', 'off')
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local config = require('config')

        t.assert_equals(config:info().status, 'ready')
        t.assert_equals(#config:info().alerts, 0)
    end)
end

g.test_checks_off_no_fiber = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :set_instance_option('i-001', 'config.checks', 'off')
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')

        local function has_check_fiber()
            for _, f in pairs(fiber.info()) do
                if f.name == 'config.checks' then
                    return true
                end
            end
            return false
        end

        t.assert_not(has_check_fiber(),
            'Check fiber should not run when checks are off')
    end)
end

g.test_no_fiber_without_checks = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')

        local function has_check_fiber()
            for _, f in pairs(fiber.info()) do
                if f.name == 'config.checks' then
                    return true
                end
            end
            return false
        end

        t.assert_not(has_check_fiber(),
            'Check fiber should not run when no checks are registered')
    end)
end

-- }}} checks: off

-- {{{ Validation

g.test_checks_invalid_string = function()
    local instance_config = require('internal.config.instance_config')
    local iconfig = {
        config = {
            checks = 'on',
        },
    }
    local exp_err = '[instance_config] config.checks: Data does not match ' ..
        'selected union variant #1 (variant #1: Got on, but only the ' ..
        'following values are allowed: off)'
    t.assert_error_msg_equals(exp_err, function()
        instance_config:validate(iconfig)
    end)
end

-- }}} Validation

-- {{{ Readahead check

g.test_readahead_alert = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :set_instance_option('i-001', 'iproto', {
            readahead = 1048512,
        })
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local config = require('config')

        local alerts = config:new_alerts_namespace('checks')
        local alert = alerts:get('readahead')
        t.assert_not_equals(alert, nil, 'Readahead alert must be present')
        t.assert_str_contains(alert.message, 'readahead')
    end)

    local config_2 = cbuilder:new(config)
        :set_instance_option('i-001', 'iproto', {
            readahead = 1048511,
        })
        :config()
    g.cluster:sync(config_2)

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local config = require('config')

        config:reload()

        local cond = fiber.cond()
        local watcher = box.watch('config.info', function(_, info)
            if info.status == 'ready' then
                cond:signal()
            end
        end)
        local ok = cond:wait(5)
        watcher:unregister()
        t.assert(ok, 'Timed out waiting for ready status')

        local alerts = config:new_alerts_namespace('checks')
        t.assert_is(alerts:get('readahead'), nil,
            'Readahead alert must be absent after reload')
    end)
end

g.test_readahead_no_alert_below_threshold = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :set_instance_option('i-001', 'iproto', {
            readahead = 1048511,
        })
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local config = require('config')

        local alerts = config:new_alerts_namespace('checks')
        t.assert_is(alerts:get('readahead'), nil,
            'Readahead alert must be absent below threshold')
    end)
end

g.test_readahead_alert_check_disabled = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :set_instance_option('i-001', 'config.checks', {
            readahead = false,
        })
        :set_instance_option('i-001', 'iproto', {
            readahead = 1048512,
        })
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local config = require('config')

        local alerts = config:new_alerts_namespace('checks')
        t.assert_is(alerts:get('readahead'), nil,
            'Readahead alert must be absent when check is disabled')
    end)
end

g.test_readahead_alert_via_box_cfg = function(g)
    local config = cbuilder:new():add_instance('i-001', {}):config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local checks = require('internal.config.applier.checks')

        checks._internal.set_check_interval(1)

        t.assert_equals(box.cfg.readahead, 16320)

        local alerts = box.info.config.alerts
        local found = false
        for _, alert in ipairs(alerts) do
            if alert.message ~= nil and
                    string.find(alert.message, 'readahead', 1,
                        true) ~= nil then
                found = true
                break
            end
        end
        t.assert_not(found,
            'Readahead alert must be absent with default readahead')

        box.cfg{readahead = 1048520}

        local cond = fiber.cond()
        local watcher = box.watch('config.info', function(_, info)
            if info.status == 'check_warnings' then
                cond:signal()
            end
        end)
        local ok = cond:wait(5)
        watcher:unregister()
        t.assert(ok, 'Timed out waiting for check_warnings status')

        local alerts2 = box.info.config.alerts
        local found2 = false
        for _, alert in ipairs(alerts2) do
            if alert.message ~= nil and
                    string.find(alert.message, 'readahead', 1,
                        true) ~= nil then
                found2 = true
                break
            end
        end
        t.assert(found2,
            'Readahead alert not found after box.cfg change')
    end)
end

-- }}} Readahead check
