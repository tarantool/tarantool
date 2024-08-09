local function apply_rt_access(config_module)
    local configdata = config_module._configdata
    local credentials = configdata:get('credentials') or {}
    credentials.roles = credentials.roles or {}
    credentials.users = credentials.users or {}

    local role_lua_call_priv = {}
    local user_lua_call_priv = {}

    local function process_lua_call(privs, user_or_role_name, user_or_role_data)
        privs[user_or_role_name] = {}
        local user_roles = user_or_role_data.roles or {}

        if user_or_role_data.privileges then
            for _, privilege in pairs(user_or_role_data.privileges) do
                local permissions = privilege.permissions or {}
                local lua_calls = privilege.lua_call or {}

                local has_execute = false
                for _, perm in ipairs(permissions) do
                    if perm == "execute" then
                        has_execute = true
                        break
                    end
                end

                if has_execute then
                    for _, call in ipairs(lua_calls) do
                        privs[user_or_role_name][call] = true
                    end
                end
            end
            for _, role_name in ipairs(user_roles) do
                for call, _ in pairs(role_lua_call_priv[role_name] or {}) do
                    privs[user_or_role_name][call] = true
                end
            end
        end
    end

    for role_name, role_data in pairs(credentials.roles) do
        process_lua_call(role_lua_call_priv, role_name, role_data)
    end

    for user_name, user_data in pairs(credentials.users) do
        process_lua_call(user_lua_call_priv, user_name, user_data)
    end

    box.internal.reset_lua_call()
    for user_name, funcs in pairs(user_lua_call_priv) do
        for func_name, _ in pairs(funcs) do
            if func_name == "all" then
                box.internal.grant_lua_call(user_name, '')
            else
                box.internal.grant_lua_call(user_name, func_name)
            end
        end
    end
end

return {
    name = 'runtime_priv',
    apply = apply_rt_access
}
