local log = require('internal.config.utils.log')

local last_loaded = {}
local last_loaded_names_ordered = {}

local function stop_roles(roles_to_skip)
    for id = #last_loaded_names_ordered, 1, -1 do
        local role_name = last_loaded_names_ordered[id]
        if roles_to_skip == nil or roles_to_skip[role_name] == nil then
            log.verbose('roles.apply: stop role ' .. role_name)
            local ok, err = pcall(last_loaded[role_name].stop)
            if not ok then
                error(('Error stopping role %s: %s'):format(role_name, err), 0)
            end
        end
    end
end

local function apply(config)
    local configdata = config._configdata
    local role_names = configdata:get('roles', {use_default = true})
    if role_names == nil or next(role_names) == nil then
        stop_roles()
        return
    end

    -- Remove duplicates.
    local roles = {}
    local roles_ordered = {}
    for _, role_name in pairs(role_names) do
        if roles[role_name] == nil then
            table.insert(roles_ordered, role_name)
        end
        roles[role_name] = true
    end

    -- Stop removed roles.
    stop_roles(roles)

    -- Run roles.
    local roles_cfg = configdata:get('roles_cfg', {use_default = true}) or {}
    local loaded = {}
    local loaded_names_ordered = {}

    -- Load roles.
    for _, role_name in ipairs(roles_ordered) do
        local role = last_loaded[role_name]
        if not role then
            log.verbose('roles.apply: load role ' .. role_name)
            role = require(role_name)
            local funcs = {'validate', 'apply', 'stop'}
            for _, func_name in pairs(funcs) do
                if type(role[func_name]) ~= 'function' then
                    local err = 'Role %s does not contain function %s'
                    error(err:format(role_name, func_name), 0)
                end
            end
        end
        loaded[role_name] = role
        table.insert(loaded_names_ordered, role_name)
    end

    -- Validate configs for all roles.
    for _, role_name in ipairs(roles_ordered) do
        local ok, err = pcall(loaded[role_name].validate, roles_cfg[role_name])
        if not ok then
            error(('Wrong config for role %s: %s'):format(role_name, err), 0)
        end
    end

    -- Apply configs for all roles.
    for _, role_name in ipairs(roles_ordered) do
        log.verbose('roles.apply: apply config for role ' .. role_name)
        local ok, err = pcall(loaded[role_name].apply, roles_cfg[role_name])
        if not ok then
            error(('Error applying role %s: %s'):format(role_name, err), 0)
        end
    end

    last_loaded = loaded
    last_loaded_names_ordered = loaded_names_ordered
end

return {
    name = 'roles',
    apply = apply,
}
