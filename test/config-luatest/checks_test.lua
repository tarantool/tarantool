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

-- {{{ Mixed sync/async spaces check

-- Space setup types:
--   mix  — one async space + one sync space
--   async — one async space only
--   sync  — one sync space only
--   nil     — no user spaces
local function run_mixed_sync_test(g, checks_cfg, space_setup, exp_contains)
    local builder = cbuilder:new():add_instance('i-001', {})
    if checks_cfg ~= nil then
        builder = builder:set_instance_option('i-001', 'config.checks',
            checks_cfg)
    end
    local config = builder:config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function(space_setup, exp_contains)
        local t = require('luatest')
        local checks = require('internal.config.applier.checks')

        -- Create spaces according to the setup type.
        if space_setup == 'mix' then
            box.schema.create_space('async_space')
            box.schema.create_space('sync_space', {is_sync = true})
        elseif space_setup == 'async' then
            box.schema.create_space('async_space')
        elseif space_setup == 'sync' then
            box.schema.create_space('sync_space', {is_sync = true})
        end

        local config = require('config')
        checks.apply(config)

        local alerts = config:new_alerts_namespace('checks')
        local alert = alerts:get('mixed_sync_async_spaces')
        if exp_contains ~= nil then
            t.assert_not_equals(alert, nil,
                'Mixed sync/async alert must be present')
            t.assert_str_contains(alert.message, exp_contains)
        else
            t.assert_is(alert, nil,
                'Mixed sync/async alert must be absent')
        end
    end, {space_setup, exp_contains})
end

g.test_mixed_sync_alert_mix_sync_and_async = function(g)
    run_mixed_sync_test(g,
        {mixed_sync_async_spaces = true},
        'mix',
        'different is_sync')
end

g.test_mixed_sync_alert_only_async = function(g)
    run_mixed_sync_test(g,
        {mixed_sync_async_spaces = true},
        'async',
        nil)
end

g.test_mixed_sync_alert_only_sync = function(g)
    -- Only sync user space, no async user spaces — no alert.
    run_mixed_sync_test(g,
        {mixed_sync_async_spaces = true},
        'sync',
        nil)
end

g.test_mixed_sync_alert_check_disabled = function(g)
    run_mixed_sync_test(g,
        {mixed_sync_async_spaces = false},
        'mix',
        nil)
end

g.test_mixed_sync_alert_enabled_by_default = function(g)
    run_mixed_sync_test(g,
        nil,
        'mix',
        'different is_sync')
end

-- }}} Mixed sync/async spaces check

-- {{{ Fiber-based checks

g.test_fiber_detects_new_sync_space = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local config = require('config')
        local checks = require('internal.config.applier.checks')

        t.assert_equals(config:info().status, 'ready')

        checks._internal.set_check_interval(1)

        box.schema.create_space('sync_space', {is_sync = true})

        local cond = fiber.cond()
        local watcher = box.watch('config.info', function(_, info)
            if info.status == 'check_warnings' then
                cond:signal()
            end
        end)
        local ok = cond:wait(5)
        watcher:unregister()
        t.assert(ok, 'Timed out waiting for check_warnings status')

        local alerts = config:new_alerts_namespace('checks')
        local alert = alerts:get('mixed_sync_async_spaces')
        t.assert_not_equals(alert, nil,
            'Alert not found after fiber detected sync space')
        t.assert_str_contains(alert.message, 'different is_sync')
    end)
end

g.test_fiber_drops_alert_after_space_drop = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local config = require('config')
        local checks = require('internal.config.applier.checks')

        box.schema.create_space('sync_space', {is_sync = true})

        checks._internal.set_check_interval(1)

        local cond = fiber.cond()
        local watcher = box.watch('config.info', function(_, info)
            if info.status == 'check_warnings' then
                cond:signal()
            end
        end)
        local ok = cond:wait(5)
        t.assert(ok, 'Timed out waiting for check_warnings status')

        box.space.sync_space:drop()

        local cond2 = fiber.cond()
        local watcher2 = box.watch('config.info', function(_, info)
            if info.status == 'ready' then
                cond2:signal()
            end
        end)
        ok = cond2:wait(5)
        watcher:unregister()
        watcher2:unregister()
        t.assert(ok, 'Timed out waiting for ready status')

        local alerts = config:new_alerts_namespace('checks')
        t.assert_is(alerts:get('mixed_sync_async_spaces'), nil,
            'Mixed sync/async alert must be absent after space drop')
    end)
end

-- }}} Fiber-based checks
