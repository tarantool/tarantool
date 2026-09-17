local role
local is_applied = false

local function get_role()
    if role == nil then
        role = require('roles.metrics-export')
    end
    return role
end

local function is_role_configured(configdata)
    local roles = configdata:get('roles', {use_default = true}) or {}
    for _, role_name in pairs(roles) do
        if role_name == 'roles.metrics-export' then
            return true
        end
    end
    return false
end

local function apply(config)
    local configdata = config._configdata
    if not is_role_configured(configdata) then
        return
    end
    local export_cfg = configdata:get('metrics.export', {
        use_default = true,
    }) or {}
    if next(export_cfg) ~= nil then
        error('metrics.export cannot be used together with role ' ..
              '"roles.metrics-export"', 0)
    end
end

local function post_apply(config)
    local configdata = config._configdata
    -- The role applier runs first and owns the module when it is configured
    -- as a generic role for backward compatibility.
    if is_role_configured(configdata) then
        is_applied = false
        return
    end

    local export_cfg = configdata:get('metrics.export', {
        use_default = true,
    }) or {}

    if next(export_cfg) == nil then
        if is_applied then
            get_role().stop()
            is_applied = false
        end
    else
        get_role().apply(export_cfg)
        is_applied = true
    end
end

return {
    name = 'metrics.export',
    apply = apply,
    post_apply = post_apply,
}
