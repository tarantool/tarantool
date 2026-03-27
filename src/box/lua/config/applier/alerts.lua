-- System checks applier.
--
-- This applier performs various system checks and generates
-- alerts if issues are detected. New checks can be added
-- by adding functions to the `checks` table.

local fio = require('fio')

-- {{{ Alert helpers

-- Check whether a specific alert is hidden in the configuration.
local function alert_is_hidden(configdata, alert_key)
    local alert_visibility = configdata:get('alerts.' .. alert_key,
        {use_default = true})
    if alert_visibility == 'hide' then
        return true
    end
    if alert_visibility == 'show' then
        return false
    end

    -- If not explicitly set, check the default visibility.
    local default_visibility = configdata:get('alerts.default',
        {use_default = true})
    return default_visibility == 'hide'
end

-- }}} Alert helpers

-- {{{ THP (Transparent Huge Pages) check

local THP_ALERT_KEY = 'transparent_huge_pages'
local THP_SYSFS_PATH = '/sys/kernel/mm/transparent_hugepage/enabled'

-- For testing purposes.
local thp_sysfs_path = THP_SYSFS_PATH

-- Read current THP mode from sysfs.
-- Returns 'always', 'madvise', 'never' or nil (file doesn't exist).
local function get_thp_mode()
    if not fio.path.exists(thp_sysfs_path) then
        return nil
    end

    local fh = fio.open(thp_sysfs_path, {'O_RDONLY'})
    if fh == nil then
        return nil
    end

    local content = fh:read(256)
    fh:close()
    if content == nil then
        return nil
    end

    -- Format: "always [madvise] never".
    return content:match('%[(%a+)%]')
end

local function check_thp(config, configdata)
    if alert_is_hidden(configdata, THP_ALERT_KEY) then
        config._aboard:drop(THP_ALERT_KEY)
        return
    end

    local mode = get_thp_mode()
    if mode == nil or mode == 'never' then
        config._aboard:drop(THP_ALERT_KEY)
        return
    end

    config._aboard:set({
        type = 'warn',
        message = ('Transparent Huge Pages (THP) are set to "%s". ' ..
            'This may cause latency spikes and memory overhead. ' ..
            'Consider disabling THP: ' ..
            'echo never > /sys/kernel/mm/transparent_hugepage/enabled')
            :format(mode),
    }, {
        key = THP_ALERT_KEY,
    })
end

-- }}} THP (Transparent Huge Pages) check

-- {{{ System checks registry

-- List of system checks to perform.
--
-- Each check is a function that takes (config, configdata) and
-- may set or drop alerts using config._aboard.
--
-- To add a new check, add a function to this table:
--   checks.my_check_name = function(config, configdata) ... end
local checks = {
    transparent_huge_pages = check_thp,
}

-- }}} System checks registry

local function apply(config)
    local configdata = config._configdata

    for _key, check in pairs(checks) do
        check(config, configdata)
    end
end

return {
    name = 'alerts',
    apply = apply,
    _internal = {
        set_thp_sysfs_path = function(path)
            thp_sysfs_path = path
        end,
    },
}
