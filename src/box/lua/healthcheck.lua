local fiber = require('fiber')
local utils = require('internal.utils')

local function get_config()
    local config = package.loaded.config
    if type(config) ~= 'table' or type(config.info) ~= 'function' then
        return nil
    end
    return config
end

local health_alerts_namespace

local function get_health_alerts_namespace()
    if health_alerts_namespace == nil then
        local config = get_config()
        if config ~= nil and config._aboard ~= nil then
            health_alerts_namespace = config._aboard:new_namespace('health')
        end
    end

    return health_alerts_namespace
end

local health_checks = {
    liveness = {
        alert_prefix = 'Liveness',
        alerts = {},
    },
    readiness = {
        alert_prefix = 'Readiness',
        alerts = {},
    },
}

-- {{{ Alert helpers

local function alert_key(alert_code)
    return alert_code:gsub(':', '.')
end

local function alert_message(kind, name, check)
    local health_check = health_checks[kind]
    assert(health_check ~= nil)

    local reason = check.reason or check.status

    return ('%s health check %q failed: %s'):format(health_check.alert_prefix,
                                                    name, reason)
end

local function set_alert(kind, name, message, code)
    assert(health_checks[kind] ~= nil)

    local namespace = get_health_alerts_namespace()
    if namespace == nil then
        return
    end

    local key = alert_key(code)
    local issued = health_checks[kind].alerts[name]

    if issued ~= nil and issued.message == message and issued.key == key then
        return
    end

    if issued ~= nil and issued.key ~= key then
        namespace:unset(issued.key)
    end

    namespace:set(key, {
        message = message,
        alert_code = code,
    })

    health_checks[kind].alerts[name] = {
        key = key,
        message = message,
    }
end

local function unset_alert(kind, name)
    assert(health_checks[kind] ~= nil)

    local issued = health_checks[kind].alerts[name]
    if issued == nil then
        return
    end

    local namespace = get_health_alerts_namespace()
    if namespace ~= nil then
        namespace:unset(issued.key)
    end
    health_checks[kind].alerts[name] = nil
end

local function sync_alerts(kind, results, ok_status)
    assert(health_checks[kind] ~= nil)

    local failed = {}

    for name, check in pairs(results) do
        if check.status ~= ok_status then
            failed[name] = true
            if check.alert_code ~= nil then
                set_alert(kind, name, alert_message(kind, name, check),
                          check.alert_code)
            end
        end
    end

    for name, _ in pairs(health_checks[kind].alerts) do
        if not failed[name] then
            unset_alert(kind, name)
        end
    end
end

-- }}} Alert helpers

local checks = {
    liveness = {},
    readiness = {},
}

local syncing_alerts = false
local evaluating_health_checks = false

local function evaluate_health_checks(fn)
    if evaluating_health_checks then
        error('recursive health check evaluation is forbidden', 0)
    end

    evaluating_health_checks = true
    local ok, res = pcall(fn)
    evaluating_health_checks = false
    if not ok then
        error(res, 0)
    end
    return res
end

local function default_alert_code(kind, name)
    return ('health.%s.%s'):format(kind, name)
end

local HEALTHCHECK_OPTION_TYPES = {
    if_exists = 'boolean',
    if_not_exists = 'boolean',
    alert = 'boolean',
    alert_code = 'string',
}

local LIVENESS_PROBE_TYPES = {
    name = 'string',
    check = 'function',
    alert = 'boolean',
    alert_code = 'string',
}

local function add_check(kind, name, fn, opts)
    opts = opts or {}

    utils.check_param(name, 'name', 'string')
    utils.check_param(fn, 'fn', 'function')
    utils.check_param_table(opts, HEALTHCHECK_OPTION_TYPES)

    local registry = checks[kind]
    if registry[name] ~= nil and not opts.if_not_exists then
        return false, ('health check %q already exists'):format(name)
    end

    if registry[name] == nil then
        local alert_code
        if opts.alert ~= false then
            alert_code = opts.alert_code or default_alert_code(kind, name)
        end
        registry[name] = {
            fn = fn,
            alert_code = alert_code,
            status = nil,
        }
    end

    return true
end

local function remove_check(kind, name, opts)
    opts = opts or {}

    utils.check_param(name, 'name', 'string')
    utils.check_param_table(opts, HEALTHCHECK_OPTION_TYPES)

    local registry = checks[kind]
    if registry[name] == nil and not opts.if_exists then
        return false, ('health check %q does not exist'):format(name)
    end

    unset_alert(kind, name)
    registry[name] = nil

    return true
end

local function evaluate(check, ok_status, fail_status, use_degraded)
    local fn = check.fn
    local alert_code = check.alert_code
    local function fail(reason)
        local status = fail_status
        if use_degraded and
           (check.status == ok_status or check.status == 'degraded') then
            status = 'degraded'
        end
        check.status = status
        return {
            status = status,
            reason = reason,
            alert_code = alert_code,
        }
    end
    local csw = fiber.self():csw()
    local ok, res, reason = pcall(fn)

    if not ok then
        return fail(tostring(res))
    end

    if fiber.self():csw() ~= csw then
        return fail('health check must not yield')
    end

    if res == true then
        check.status = ok_status
        return { status = ok_status }
    end

    if (res == false or res == nil) and type(reason) == 'string' then
        return fail(reason)
    end

    return fail('health check must return true or false, <string>')
end

local function evaluate_registry(kind, ok_status, fail_status, skip)
    local res = {}
    local use_degraded = kind == 'readiness'
    for name, check in pairs(checks[kind]) do
        if skip == nil or not skip[name] then
            res[name] = evaluate(check, ok_status, fail_status, use_degraded)
        end
    end
    return res
end

local function liveness()
    local registry_checks = evaluate_registry('liveness', 'ok', 'failed')
    sync_alerts('liveness', registry_checks, 'ok')

    local res = {
        verdict = true,
        checks = registry_checks,
    }

    for _, check in pairs(res.checks) do
        if check.status == 'failed' then
            res.verdict = false
        end
    end
    return res
end

local function readiness()
    local registry_checks = evaluate_registry('readiness', 'ready',
                                              'not_ready')
    sync_alerts('readiness', registry_checks, 'ready')

    local res = {
        status = true,
        checks = registry_checks,
    }

    for _, check in pairs(res.checks) do
        if check.status ~= 'ready' then
            res.status = false
            break
        end
    end
    return res
end

local health = {}

function health.add_health_check(name, fn, opts)
    return add_check('readiness', name, fn, opts)
end

function health.remove_health_check(name, opts)
    return remove_check('readiness', name, opts)
end

function health.liveness_probe(opts)
    utils.check_param(opts, 'opts', 'table')
    utils.check_param_table(opts, LIVENESS_PROBE_TYPES)
    return add_check('liveness', opts.name, opts.check, {
        alert = opts.alert,
        alert_code = opts.alert_code,
    })
end

function health.remove_liveness_probe(name, opts)
    return remove_check('liveness', name, opts)
end

function health.liveness()
    return evaluate_health_checks(liveness)
end

function health.readiness()
    return evaluate_health_checks(readiness)
end

function health._sync_alerts(opts)
    if syncing_alerts or evaluating_health_checks then
        return
    end
    syncing_alerts = true
    opts = opts or {}
    local ok, err = pcall(function()
        evaluate_health_checks(function()
            local liveness_checks = evaluate_registry('liveness', 'ok',
                                                     'failed')
            sync_alerts('liveness', liveness_checks, 'ok')

            local readiness_checks = evaluate_registry('readiness', 'ready',
                                                       'not_ready', opts.skip)
            sync_alerts('readiness', readiness_checks, 'ready')
        end)
    end)
    syncing_alerts = false
    if not ok then
        error(err, 0)
    end
end

function health.info()
    return evaluate_health_checks(function()
        return {
            liveness = liveness(),
            readiness = readiness(),
        }
    end)
end

return health
