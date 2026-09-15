local clock = require('clock')
local digest = require('digest')
local ffi = require('ffi')
local fiber = require('fiber')
local fio = require('fio')
local log = require('log')

ffi.cdef([[
    int ssl_cert_get_not_after(const char *path, int64_t *timestamp);
]])

local CHECK_INTERVAL = 12 * 60 * 60
local EXPIRY_WARNING_PERIOD = 24 * 60 * 60
-- Include one polling interval to guarantee that the alert is present no
-- later than EXPIRY_WARNING_PERIOD before the certificate expires.
local ALERT_THRESHOLD = CHECK_INTERVAL + EXPIRY_WARNING_PERIOD

local function scalar(value)
    if type(value) == 'table' then
        value = value[1]
    end
    if type(value) ~= 'string' or value == '' then
        return nil
    end
    return value
end

local function add(records, path, component)
    path = scalar(path)
    if path == nil then
        return
    end
    path = fio.abspath(path)
    local record = records[path]
    if record == nil then
        record = {components = {}}
        records[path] = record
    end
    record.components[component] = true
end

local function alert_key(path)
    return 'certificate_' .. digest.sha256_hex(path):sub(1, 16)
end

local function read_expiration(path)
    local timestamp = ffi.new('int64_t[1]')
    if ffi.C.ssl_cert_get_not_after(path, timestamp) ~= 0 then
        local err = box.error.last()
        return nil, err ~= nil and tostring(err) or 'unknown error'
    end
    return tonumber(timestamp[0])
end

local function sorted_components(components)
    local result = {}
    for component in pairs(components) do
        table.insert(result, component)
    end
    table.sort(result)
    return result
end

-- Each consumer owns its alert namespace and checker fiber.
local function new(opts)
    local config
    local alerts
    local checker_fiber
    local alert_keys = {}
    local CHECKER_ALERT_KEY = 'checker_failure'

    local function discover()
        local records = {}
        opts.discover(config, records, add)
        return records
    end

    local function update_alert(path, record, expiration, err, now)
        local key = alert_key(path)
        local components = sorted_components(record.components)
        local usage = (' (components: %s)')
            :format(table.concat(components, ', '))
        if err ~= nil then
            return key, alerts:set(key, {
                message = ('Unable to check SSL certificate %q: %s%s')
                    :format(path, err, usage),
                path = path,
                components = components,
            })
        end
        if expiration - now > ALERT_THRESHOLD then
            return nil, false
        end
        local expired = expiration <= now
        local message = ('SSL certificate %q %s at %s%s'):format(
            path, expired and 'expired' or 'expires',
            os.date('!%Y-%m-%dT%H:%M:%SZ', expiration), usage)
        return key, alerts:set(key, {
            message = message,
            path = path,
            expiration_timestamp = expiration,
            components = components,
        })
    end

    local function check(now)
        local records = discover()
        now = now or clock.time()
        local next_alert_keys = {}
        local alerts_changed = false
        for path, record in pairs(records) do
            local expiration, err = read_expiration(path)
            local key, changed = update_alert(path, record, expiration, err,
                                              now)
            alerts_changed = alerts_changed or changed
            if key ~= nil then
                next_alert_keys[key] = true
            end
        end
        for key in pairs(alert_keys) do
            if not next_alert_keys[key] then
                alerts_changed = alerts:unset(key) or alerts_changed
            end
        end
        alert_keys = next_alert_keys
        if alerts_changed and opts.on_change ~= nil then
            opts.on_change(config)
        end
    end

    local function set_status_noexcept()
        if opts.on_change ~= nil then
            pcall(opts.on_change, config)
        end
    end

    local function check_noexcept(now)
        local ok, err = pcall(check, now)
        if ok then
            local unset_ok, changed = pcall(alerts.unset, alerts,
                                           CHECKER_ALERT_KEY)
            if unset_ok and changed then
                set_status_noexcept()
            end
            return true
        end
        if opts.log_errors ~= false then
            log.error('Failed to check SSL certificate expiration: %s', err)
        end
        local message = ('Unable to run SSL certificate checker: %s')
            :format(err)
        local set_ok, changed = pcall(alerts.set, alerts, CHECKER_ALERT_KEY, {
            message = message,
        })
        if set_ok and changed then
            set_status_noexcept()
        end
        return false
    end

    local function checker_loop()
        fiber.name('ssl_certificate.checker')
        while true do
            fiber.sleep(CHECK_INTERVAL)
            check_noexcept()
        end
    end

    local function start_checker()
        if checker_fiber == nil or checker_fiber:status() == 'dead' then
            checker_fiber = fiber.new(checker_loop)
        end
    end

    local function apply(config_module)
        config = config_module
        if alerts == nil then
            alerts = opts.new_alerts(config)
        end
        check_noexcept()
        start_checker()
    end

    return {
        apply = apply,
        _check = check,
        _check_noexcept = check_noexcept,
        _discover = discover,
        _read_expiration = read_expiration,
        _constants = {
            check_interval = CHECK_INTERVAL,
            expiry_warning_period = EXPIRY_WARNING_PERIOD,
            alert_threshold = ALERT_THRESHOLD,
        },
    }
end

local discoveries = {}
local checker = new({
    discover = function(config, records, add_certificate)
        for _, discover in ipairs(discoveries) do
            discover(config, records, add_certificate)
        end
    end,
    new_alerts = function(config)
        return config:new_alerts_namespace('ssl_certificate')
    end,
    on_change = function(config)
        config:_set_status_based_on_alerts()
    end,
})

-- Extensions register their certificate sources at initialization.
checker.register_discovery = function(discover)
    assert(type(discover) == 'function')
    for _, registered in ipairs(discoveries) do
        if registered == discover then
            return
        end
    end
    table.insert(discoveries, discover)
end

checker.register_discovery(function(config, records, add_certificate)
    add_certificate(records, config:get('iproto.ssl.ssl_cert'), 'iproto')
end)

checker.new = new

return checker
