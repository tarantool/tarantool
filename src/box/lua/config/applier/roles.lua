local log = require('internal.config.utils.log')
local fiber = require('fiber')

local roles_state = {
    -- Loaded roles, where the key is the role name and the value is the
    -- role object.
    last_loaded = {},

    -- Ordered list of roles, where the order is determined by the
    -- dependencies between roles.
    last_roles_ordered = {},

    -- The box.status watcher, used to call on_event callbacks.
    box_status_watcher = nil,

    -- Last values received from box.status watcher.
    last_box_status_value = nil,

    -- Table with the last applied roles configs, where the key is the role name
    -- and the value is the config for the role.
    last_roles_cfg = nil,
}

-- Function decorator that is used to prevent call_on_event_callbacks() from
-- being called concurrently by watcher and post_apply().
local lock = fiber.channel(1)
local function locked(f)
    return function(...)
        lock:put(true)
        local status, err = pcall(f, ...)
        lock:get()
        if not status then
            error(err, 0)
        end
    end
end

-- Call all the on_event callbacks from roles in order.
local call_on_event_callbacks = locked(function(roles_cfg, key, value)
    local roles_ordered = roles_state.last_roles_ordered
    local roles = roles_state.last_loaded
    for _, role_name in ipairs(roles_ordered) do
        local role = roles[role_name]
        if role.on_event ~= nil then
            log.verbose(('roles.on_event: calling callback for role ' ..
                         '"%s"'):format(role_name))
            local ok, err = pcall(role.on_event,
                                  roles_cfg[role_name], key, value)
            if not ok then
                log.error(('roles.on_event: callback for role ' ..
                           '"%s" failed: %s'):format(role_name, err))
            end
        end
    end
end)

local function box_status_watcher()
    return box.watch('box.status', function(key, value)
        assert(key == 'box.status')

        -- Store the last value to be able to call on_event callbacks
        -- when the config is changed.
        roles_state.last_box_status_value = table.deepcopy(value)

        -- Config is not fully applied yet, don't call on_event callbacks.
        -- We will call them later in post_apply.
        if roles_state.last_roles_cfg == nil then
            return
        end

        call_on_event_callbacks(roles_state.last_roles_cfg, 'box.status', value)
    end)
end

local function apply(_config)
    if roles_state.box_status_watcher == nil then
        roles_state.box_status_watcher = box_status_watcher()

        local deadline = fiber.time() + 10
        while roles_state.last_box_status_value == nil do
            fiber.sleep(0.01)
            if fiber.time() > deadline then
                error('Timeout reached while waiting for box_status_watcher', 0)
            end
        end
    end
end

local function stop_roles(roles_to_skip)
    local roles_to_stop = {}
    for id = #roles_state.last_roles_ordered, 1, -1 do
        local role_name = roles_state.last_roles_ordered[id]
        if roles_to_skip == nil or roles_to_skip[role_name] == nil then
            table.insert(roles_to_stop, role_name)
        end
    end
    if #roles_to_stop == 0 then
        return
    end
    local deps = {}
    for role_name in pairs(roles_to_skip or {}) do
        local role = roles_state.last_loaded[role_name] or {}
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
        log.verbose('roles.post_apply: stop role ' .. role_name)
        local ok, err = pcall(roles_state.last_loaded[role_name].stop)
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

local function post_apply(config)
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
        local role = roles_state.last_loaded[role_name]
        if not role then
            log.verbose('roles.post_apply: load role ' .. role_name)
            role = require(role_name)
            if type(role) ~= 'table' then
                local err = 'Unable to use module %s as a role: ' ..
                    'expected table, got %s'
                error(err:format(role_name, type(role)), 0)
            end
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
        log.verbose('roles.post_apply: apply config for role ' .. role_name)
        local ok, err = pcall(loaded[role_name].apply, roles_cfg[role_name])
        if not ok then
            error(('Error applying role %s: %s'):format(role_name, err), 0)
        end
    end

    roles_state.last_loaded = loaded
    roles_state.last_roles_ordered = roles_ordered
    roles_state.last_roles_cfg = table.deepcopy(roles_cfg)

    -- box.status watcher values should be already set, because the watcher
    -- has been registered during apply.
    assert(roles_state.last_box_status_value ~= nil)

    -- Call on_event callbacks after the config is fully applied.
    call_on_event_callbacks(roles_state.last_roles_cfg, 'config.apply',
                            roles_state.last_box_status_value)
end

return {
    name = 'roles',
    apply = apply,
    post_apply = post_apply,
}
