local log = require('internal.config.utils.log')

local last_loaded = {}
local last_roles_ordered = {}

local function stop_roles(roles_to_skip)
    local roles_to_stop = {}
    for id = #last_roles_ordered, 1, -1 do
        local role_name = last_roles_ordered[id]
        if roles_to_skip == nil or roles_to_skip[role_name] == nil then
            table.insert(roles_to_stop, role_name)
        end
    end
    if #roles_to_stop == 0 then
        return
    end
    local deps = {}
    for role_name in pairs(roles_to_skip or {}) do
        local role = last_loaded[role_name] or {}
        -- There is no need to check transitive dependencies for roles_to_skip,
        -- because they were already checked when roles were started, i.e. if
        -- role A depends on role B, which depends on role C, and we stop
        -- role C, then we will get the error that role B depends on role C.
        for _, dep in pairs(role.dependencies or {}) do
            deps[dep] = deps[dep] or {}
            table.insert(deps[dep], role_name)
        end
    end
    for _, role_name in ipairs(roles_to_stop) do
        if deps[role_name] ~= nil then
            local err
            if #deps[role_name] == 1 then
                err =('role %q depends on it'):format(deps[role_name][1])
            else
                local names = {}
                for _, v in ipairs(deps[role_name]) do
                    table.insert(names, ("%q"):format(v))
                end
                local names_str = table.concat(names, ', ')
                err = ('roles %s depend on it'):format(names_str)
            end
            error(('Role %q cannot be stopped because %s'):format(role_name,
                                                                  err), 0)
        end
    end
    for _, role_name in ipairs(roles_to_stop) do
        log.verbose('roles.apply: stop role ' .. role_name)
        local ok, err = pcall(last_loaded[role_name].stop)
        if not ok then
            error(('Error stopping role %s: %s'):format(role_name, err), 0)
        end
    end
end

local function resort_roles(original_order, roles)
    local ordered = {}

    -- Needed to detect circular dependencies.
    local to_add = {}

    -- To skip already added roles.
    local added = {}

    local function add_role(role_name)
        if added[role_name] then
            return
        end

        to_add[role_name] = true

        for _, dep in ipairs(roles[role_name].dependencies or {}) do
            -- Detect a role that is not in the list of instance's roles.
            if not roles[dep] then
                local err = 'Role %q requires role %q, but the latter is ' ..
                            'not in the list of roles of the instance'
                error(err:format(role_name, dep), 0)
            end

            -- Detect a circular dependency.
            if to_add[dep] and role_name == dep then
                local err = 'Circular dependency: role %q depends on itself'
                error(err:format(role_name), 0)
            end
            if to_add[dep] and role_name ~= dep then
                local err = 'Circular dependency: roles %q and %q depend on ' ..
                            'each other'
                error(err:format(role_name, dep), 0)
            end

            -- Go into the recursion: add the dependency.
            add_role(dep)
        end

        to_add[role_name] = nil
        added[role_name] = true
        table.insert(ordered, role_name)
    end

    -- Keep the order, where the dependency tree doesn't obligate
    -- us to change it.
    for _, role_name in ipairs(original_order) do
        assert(roles[role_name] ~= nil)
        add_role(role_name)
    end

    return ordered
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

    -- Load roles.
    local loaded = {}
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
        if role.dependencies ~= nil and type(role.dependencies) ~= 'table' then
            local err = 'Role %q has field "dependencies" of type %s, '..
                        'array-like table or nil expected'
            error(err:format(role_name, type(role.dependencies)), 0)
        end
    end

    -- Re-sorting of roles taking into account dependencies between them.
    roles_ordered = resort_roles(roles_ordered, loaded)

    -- Validate configs for all roles.
    local roles_cfg = configdata:get('roles_cfg', {use_default = true}) or {}
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
    last_roles_ordered = roles_ordered
end

return {
    name = 'roles',
    apply = apply,
}
