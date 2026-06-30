local utils = require('internal.utils')

local checks = {
    liveness = {},
}

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

    registry[name] = nil

    return true
end

local function evaluate(check, ok_status, fail_status)
    local ok, res, reason = pcall(check.fn)

    if not ok then
        return {
            status = fail_status,
            reason = tostring(res),
            alert_code = check.alert_code,
        }
    end

    if res == true then
        return { status = ok_status }
    end

    local failed = {
        status = fail_status,
        alert_code = check.alert_code,
    }

    if (res == false or res == nil) and type(reason) == 'string' then
        failed.reason = reason
    else
        failed.reason = 'health check must return true or false, <string>'
    end

    return failed
end

local function evaluate_registry(kind, ok_status, fail_status)
    local res = {}
    for name, check in pairs(checks[kind]) do
        res[name] = evaluate(check, ok_status, fail_status)
    end
    return res
end

local function liveness()
    local registry_checks = evaluate_registry('liveness', 'ok', 'failed')

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

local health = {}

function health.liveness_probe(opts)
    utils.check_param(opts, 'opts', 'table')
    utils.check_param_table(opts, LIVENESS_PROBE_TYPES)
    return add_check('liveness', opts.name, opts.check)
end

function health.remove_liveness_probe(name, opts)
    return remove_check('liveness', name, opts)
end

function health.liveness()
    return liveness()
end

function health.info()
    return {
        liveness = liveness(),
    }
end

return health
