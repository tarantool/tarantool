local health = require('internal.healthcheck')

local function get_config()
    local config = package.loaded.config
    if type(config) ~= 'table' or type(config.info) ~= 'function' then
        return nil
    end
    return config
end

local function add_box_check()
    health.add_health_check('box', function()
        if box.info.status == 'running' then
            return true
        end
        return false, box.info.status
    end, { if_not_exists = true })
end

local function config_is_initialized(config)
    local ok, info = pcall(config.info, config)
    return ok and type(info) == 'table' and info.status ~= 'uninitialized'
end

local function add_config_check(config)
    health.add_health_check('config', function()
        local ok, info = pcall(config.info, config)
        if not ok then
            return false, tostring(info)
        end
        if type(info) ~= 'table' then
            return false, 'config info is not available'
        end

        local status = info.status
        local first_error_alert_message
        for _, alert in ipairs(info.alerts or {}) do
            if alert.type == 'error' then
                first_error_alert_message = alert.message
                break
            end
        end
        return status ~= 'check_errors', first_error_alert_message
    end, { if_not_exists = true, alert = false })
end

local function ensure()
    add_box_check()

    local config = get_config()
    if config == nil or not config_is_initialized(config) then
        health.remove_health_check('config', { if_exists = true })
        return
    end

    add_config_check(config)
end

local defaults = {}

function defaults.info()
    ensure()
    return health.info()
end

return defaults
