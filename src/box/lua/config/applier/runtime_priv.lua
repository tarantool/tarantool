-- Extract and add functions from a user or a role definition to
-- the `{[func_name] = true, <...>}` mapping `res`.
--
-- The source is `lua_call` declarations.
local function add_funcs(res, user_or_role_def)
    local privileges = user_or_role_def.privileges or {}

    for _, privilege in ipairs(privileges) do
        local permissions = privilege.permissions or {}
        local has_execute = false
        for _, perm in ipairs(permissions) do
            if perm == 'execute' then
                has_execute = true
                break
            end
        end
        if has_execute and privilege.lua_call ~= nil then
            for _, func_name in ipairs(privilege.lua_call) do
                res[func_name] = true
            end
        end
    end
end

-- Extract and add roles for the given user to the
-- `{[role_name] = true, <...>}` mapping `res`, including
-- transitively assigned (when a role is assigned to a role).
local function add_roles(res, user_or_role_def, ctx)
    for _, role_name in ipairs(user_or_role_def.roles or {}) do
        -- Detect a recursion.
        if ctx.visited[role_name] then
            error(('Recursion detected: credentials.roles.%s depends on ' ..
                'itself'):format(role_name), 0)
        end

        -- Add the role into the result.
        res[role_name] = true

        -- Add the nested roles.
        --
        -- Ignore unknown roles. For example, there is a
        -- built-in role 'super' that doesn't have to be
        -- configured.
        local role_def = ctx.roles[role_name]
        if role_def ~= nil then
            ctx.visited[role_name] = true
            add_roles(res, role_def, ctx)
            ctx.visited[role_name] = nil
        end
    end
end

-- Extract all the user's functions listed in the `lua_call`
-- directives in the user definition or its roles assigned
-- directly or transitively over the other roles.
local function extract_funcs(user_name, ctx)
    local user_def = ctx.users[user_name]

    local roles = {}
    ctx.visited = {}
    add_roles(roles, user_def, ctx)
    ctx.visited = nil

    -- Collect a full set of functions for the given user.
    --
    -- {
    --     [func_name] = true,
    --     <...>,
    -- }
    local funcs = {}
    add_funcs(funcs, user_def)
    for role_name, _ in pairs(roles) do
        local role_def = ctx.roles[role_name]
        if role_def ~= nil then
            add_funcs(funcs, role_def)
        end
    end

    return funcs
end

local function apply(config_module)
    -- Prepare a context with the configuration information to
    -- transform.
    local configdata = config_module._configdata
    local ctx = {
        roles = configdata:get('credentials.roles') or {},
        users = configdata:get('credentials.users') or {},
    }

    -- Collect a mapping from users to their granted functions.
    --
    -- {
    --     [user_name] = {
    --         [func_name] = true,
    --         <...>,
    --     },
    --     <...>
    -- }
    local res = {}
    for user_name, _ in pairs(ctx.users) do
        local funcs = extract_funcs(user_name, ctx)
        if next(funcs) ~= nil then
            res[user_name] = funcs
        end
    end

    -- Reset the runtime privileges and grant all the configured
    -- ones.
    box.internal.lua_call_runtime_priv_reset()
    for user_name, funcs in pairs(res) do
        for func_name, _ in pairs(funcs) do
            if func_name == 'all' then
                box.internal.lua_call_runtime_priv_grant(user_name, '')
            else
                box.internal.lua_call_runtime_priv_grant(user_name, func_name)
            end
        end
    end
end

return {
    name = 'runtime_priv',
    apply = apply,
}
