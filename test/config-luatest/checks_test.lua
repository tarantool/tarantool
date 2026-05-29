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

-- {{{ Transparent Huge Pages check

-- Create a mock THP sysfs file with the given mode.
local function create_thp_file(temp_dir, mode)
    local thp_file = fio.pathjoin(temp_dir, 'enabled')
    local content
    if mode == 'always' then
        content = '[always] madvise never'
    elseif mode == 'madvise' then
        content = 'always [madvise] never'
    else
        content = 'always madvise [never]'
    end
    local fh = fio.open(thp_file, {'O_CREAT', 'O_WRONLY'}, tonumber('644', 8))
    fh:write(content)
    fh:close()
    return thp_file
end

-- Run test with THP mocked to the given mode.
local function run_with_thp_mock(g, thp_mode, checks_cfg, exp_contains)
    g.temp_dir = fio.tempdir()
    local thp_file
    if thp_mode ~= nil then
        thp_file = create_thp_file(g.temp_dir, thp_mode)
    else
        thp_file = fio.pathjoin(g.temp_dir, 'no_such_file')
    end

    local builder = cbuilder:new():add_instance('i-001', {})
    if checks_cfg ~= nil then
        builder = builder:set_instance_option('i-001', 'config.checks',
            checks_cfg)
    end
    local config = builder:config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function(thp_file, exp_contains)
        local t = require('luatest')
        local checks = require('internal.config.applier.checks')
        checks._internal.set_thp_sysfs_path(thp_file)

        local config = require('config')
        checks.apply(config)

        local alerts = config:new_alerts_namespace('checks')
        local alert = alerts:get('transparent_huge_pages')
        if exp_contains ~= nil then
            t.assert_not_equals(alert, nil,
                'THP alert must be present')
            t.assert_str_contains(alert.message, exp_contains)
        else
            t.assert_is(alert, nil, 'THP alert must be absent')
        end
    end, {thp_file, exp_contains})
end

g.test_thp_alert_enabled_always = function(g)
    run_with_thp_mock(g, 'always', {transparent_huge_pages = true}, 'always')
end

g.test_thp_alert_enabled_madvise = function(g)
    run_with_thp_mock(g, 'madvise', {transparent_huge_pages = true}, 'madvise')
end

g.test_thp_alert_disabled_never = function(g)
    run_with_thp_mock(g, 'never', {transparent_huge_pages = true}, nil)
end

g.test_thp_alert_no_file = function(g)
    run_with_thp_mock(g, nil, {transparent_huge_pages = true}, nil)
end

g.test_thp_alert_check_disabled = function(g)
    run_with_thp_mock(g, 'always',
        {transparent_huge_pages = false},
        nil)
end

g.test_thp_alert_check_enabled = function(g)
    run_with_thp_mock(g, 'always',
        {transparent_huge_pages = true},
        'Transparent Huge Pages')
end

g.test_thp_alert_enabled_by_default = function(g)
    run_with_thp_mock(g, 'always', nil, 'Transparent Huge Pages')
end

-- }}} Transparent Huge Pages check

-- {{{ Fiber-based checks

g.test_fiber_detects_thp_enabled = function(g)
    g.temp_dir = fio.tempdir()
    local thp_file = create_thp_file(g.temp_dir, 'never')

    local builder = cbuilder:new():add_instance('i-001', {})
    local config = builder:config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function(thp_file)
        local t = require('luatest')
        local fiber = require('fiber')
        local fio = require('fio')
        local config = require('config')
        local checks = require('internal.config.applier.checks')

        checks._internal.set_check_interval(1)
        checks._internal.set_thp_sysfs_path(thp_file)

        local fh = fio.open(thp_file, {'O_WRONLY', 'O_TRUNC'})
        fh:write('[always] madvise never')
        fh:close()

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
        local alert = alerts:get('transparent_huge_pages')
        t.assert_not_equals(alert, nil,
            'THP alert not found after fiber detected change')
        t.assert_str_contains(alert.message, 'Transparent Huge Pages')
    end, {thp_file})
end

g.test_fiber_drops_thp_alert_after_disabled = function(g)
    g.temp_dir = fio.tempdir()
    local thp_file = create_thp_file(g.temp_dir, 'always')

    local builder = cbuilder:new():add_instance('i-001', {})
    local config = builder:config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function(thp_file)
        local t = require('luatest')
        local fiber = require('fiber')
        local fio = require('fio')
        local config = require('config')
        local checks = require('internal.config.applier.checks')

        checks._internal.set_check_interval(1)
        checks._internal.set_thp_sysfs_path(thp_file)

        local cond = fiber.cond()
        local watcher = box.watch('config.info', function(_, info)
            if info.status == 'check_warnings' then
                cond:signal()
            end
        end)
        local ok = cond:wait(5)
        t.assert(ok, 'Timed out waiting for check_warnings status')

        local fh = fio.open(thp_file, {'O_WRONLY', 'O_TRUNC'})
        fh:write('always madvise [never]')
        fh:close()

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
        t.assert_is(alerts:get('transparent_huge_pages'), nil,
            'THP alert must be absent after disabling')
    end, {thp_file})
end

g.test_fiber_stopped_when_all_checks_disabled = function(g)
    local builder = cbuilder:new():add_instance('i-001', {})
    local config = builder:config()

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

        t.assert(has_check_fiber(),
            'Check fiber should be running')
    end)

    local config_2 = cbuilder:new(config)
        :set_instance_option('i-001', 'config.checks', {
            transparent_huge_pages = false,
        })
        :config()
    g.cluster:sync(config_2)

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local config = require('config')

        config:reload()

        local function has_check_fiber()
            for _, f in pairs(fiber.info()) do
                if f.name == 'config.checks' then
                    return true
                end
            end
            return false
        end

        t.helpers.retrying({timeout = 5}, function()
            t.assert_not(has_check_fiber(),
                'Check fiber should be stopped when all checks are disabled')
        end)
    end)
end

g.test_fiber_started_when_check_enabled_via_reload = function(g)
    local builder = cbuilder:new():add_instance('i-001', {})
    builder = builder:set_instance_option('i-001', 'config.checks', {
        transparent_huge_pages = false,
    })
    local config = builder:config()

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
            'Check fiber should not run when all checks are disabled')
    end)

    local config_2 = cbuilder:new(config)
        :set_instance_option('i-001', 'config.checks', {
            transparent_huge_pages = true,
        })
        :config()
    g.cluster:sync(config_2)

    g.cluster['i-001']:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local config = require('config')

        config:reload()

        local function has_check_fiber()
            for _, f in pairs(fiber.info()) do
                if f.name == 'config.checks' then
                    return true
                end
            end
            return false
        end

        t.helpers.retrying({timeout = 5}, function()
            t.assert(has_check_fiber(),
                'Check fiber should be running after enabling check via reload')
        end)
    end)
end

-- }}} Fiber-based checks
