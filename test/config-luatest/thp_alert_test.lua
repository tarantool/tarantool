local t = require('luatest')
local fio = require('fio')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

g.after_each(function(g)
    if g.cluster ~= nil then
        g.cluster:stop()
    end
    if g.temp_dir ~= nil then
        fio.rmtree(g.temp_dir)
    end
end)

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
local function run_with_thp_mock(g, thp_mode, alerts_cfg, exp_contains)
    local thp_file
    if thp_mode ~= nil then
        g.temp_dir = fio.tempdir()
        thp_file = create_thp_file(g.temp_dir, thp_mode)
    end

    local builder = cbuilder:new():add_instance('i-001', {})
    if alerts_cfg ~= nil then
        builder = builder:set_instance_option('i-001', 'alerts', alerts_cfg)
    end
    local config = builder:config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    g.cluster['i-001']:exec(function(thp_file, exp_contains)
        local alerts = require('internal.config.applier.alerts')
        if thp_file ~= nil then
            alerts._internal.set_thp_sysfs_path(thp_file)
        end

        local config = require('config')
        alerts.apply(config)

        local alerts = box.info.config.alerts
        if exp_contains ~= nil then
            t.assert_str_contains(alerts[1].message, exp_contains)
        end
    end, {thp_file, exp_contains})
end

g.test_thp_alert_enabled_always = function(g)
    run_with_thp_mock(g, 'always', {transparent_huge_pages = 'show'}, 'always')
end

g.test_thp_alert_enabled_madvise = function(g)
    run_with_thp_mock(g, 'madvise', {transparent_huge_pages = 'show'}, 'madvise')
end

g.test_thp_alert_disabled_never = function(g)
    run_with_thp_mock(g, 'never', nil, nil)
end

g.test_thp_alert_no_file = function(g)
    run_with_thp_mock(g, nil, nil, nil)
end

g.test_thp_alert_hidden_by_config = function(g)
    run_with_thp_mock(g, 'always',
        {transparent_huge_pages = 'hide'},
        nil)
end

g.test_thp_alert_shown_by_config = function(g)
    run_with_thp_mock(g, 'always',
        {transparent_huge_pages = 'show'},
        'Transparent Huge Pages')
end

g.test_thp_alert_hidden_by_default = function(g)
    run_with_thp_mock(g, 'always', nil, nil)
end

g.test_thp_alert_override_default = function(g)
    run_with_thp_mock(g, 'always',
        {default = 'show', transparent_huge_pages = 'hide'},
        nil)
end
