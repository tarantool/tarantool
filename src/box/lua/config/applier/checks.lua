-- System checks applier.
--
-- This applier performs various system checks and generates
-- alerts if issues are detected. New checks can be added
-- by adding functions to the `checks` table.
--
-- The checks run on every config apply/reload and also periodically
-- via a background fiber, so that runtime changes are
-- detected automatically without requiring config:reload().

local fiber = require('fiber')
local log = require('internal.config.utils.log')

-- Default interval between periodic check runs (seconds).
local CHECK_INTERVAL = 60

-- Interval used by the background fiber.
local check_interval = CHECK_INTERVAL

-- Alerts namespace for checks.
local alert_ns = nil

local function drop_alert_if_exists(key)
    if alert_ns:get(key) == nil then
        return false
    end
    alert_ns:unset(key)
    return true
end

-- Return true if all checks are disabled via config.checks = 'off'.
local function checks_disabled(configdata)
    return configdata:get('config.checks', {use_default = true}) == 'off'
end

-- {{{ Readahead check

local READAHEAD_ALERT_KEY = 'readahead'
local READAHEAD_THRESHOLD = 1024 * 1024 - 64

local function check_readahead(configdata)
    if not configdata:get('config.checks.' ..
        READAHEAD_ALERT_KEY, {use_default = true}) then
        return drop_alert_if_exists(READAHEAD_ALERT_KEY)
    end

    local readahead = box.cfg.readahead
    if readahead < READAHEAD_THRESHOLD then
        return drop_alert_if_exists(READAHEAD_ALERT_KEY)
    end

    return alert_ns:set(READAHEAD_ALERT_KEY, {
        type = 'warn',
        message = ('readahead is set to %d, which is >= %d. ' ..
            'A large readahead value may cause high memory ' ..
            'consumption that is difficult to predict. ' ..
            'Consider lowering readahead.')
            :format(readahead, READAHEAD_THRESHOLD),
    })
end

-- }}} Readahead check

-- {{{ System checks registry

-- List of system checks to perform.
--
-- Each element is a {key = <str>, fn = <func>} table where:
--   key is a config.checks.* option that enables/disables the check
--   fn  is a function(configdata) that performs the check and
--       returns true if an alert was set or dropped, false otherwise
--
-- To add a new check, add an element to this array:
--   {key = MY_ALERT_KEY, fn = my_check_function},
local checks = {
    {key = READAHEAD_ALERT_KEY, fn = check_readahead},
}

-- }}} System checks registry

-- {{{ Run all checks

local function run_checks(config)
    local configdata = config._configdata
    local changed = false

    for _, check in ipairs(checks) do
        local ok, result = pcall(check.fn, configdata)
        if not ok then
            log.error(('checks: check %s failed: %s'):format(check.key, result))
        else
            changed = changed or result
        end
    end
    return changed
end

-- }}} Run all checks

-- {{{ Check if any check is enabled

-- Drop all alerts created by the checks applier.
local function drop_all_alerts()
    for _, check in ipairs(checks) do
        drop_alert_if_exists(check.key)
    end
end

local function any_check_enabled(configdata)
    if checks_disabled(configdata) then
        return false
    end
    for _, check in ipairs(checks) do
        if configdata:get('config.checks.' .. check.key,
            {use_default = true}) then
            return true
        end
    end
    return false
end

-- }}} Check if any check is enabled

-- {{{ Background fiber

-- Fiber that periodically re-runs all checks.
local check_fiber = nil
local check_cond = fiber.cond()

local function checks_fiber_f(config)
    fiber.self():name('config.checks')
    while true do
        check_cond:wait(check_interval)
        local status = config._status
        if status ~= 'startup_in_progress' and
           status ~= 'reload_in_progress' and
           not checks_disabled(config._configdata) then
            local changed = run_checks(config)
            if changed then
                config:_set_status_based_on_alerts()
            end
        end
    end
end

local function start_fiber(config)
    if check_fiber ~= nil and check_fiber:status() ~= 'dead' then
        return
    end
    check_fiber = fiber.new(checks_fiber_f, config)
end

local function stop_fiber()
    if check_fiber ~= nil and check_fiber:status() ~= 'dead' then
        check_fiber:cancel()
        check_fiber = nil
    end
end

-- }}} Background fiber

local function apply(config)
    if alert_ns == nil then
        alert_ns = config:new_alerts_namespace('checks')
    end

    local configdata = config._configdata

    if checks_disabled(configdata) then
        drop_all_alerts()
        stop_fiber()
        return
    end

    run_checks(config)
    if any_check_enabled(configdata) then
        start_fiber(config)
    else
        stop_fiber()
    end
end

return {
    name = 'checks',
    apply = apply,
    _internal = {
        set_check_interval = function(interval)
            check_interval = interval
            check_cond:signal()
        end,
        get_check_interval = function()
            return check_interval
        end,
    },
}
