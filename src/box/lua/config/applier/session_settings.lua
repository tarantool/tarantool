-- Collect a `{[user_name] = idle_timeout}` mapping from the `session.users`
-- section. A zero timeout is passed through and treated as "no timeout" by
-- the IPROTO layer; a negative one is rejected here.
local function extract_idle_timeouts(configdata)
    local users = configdata:get('session.users') or {}

    local res = {}
    for user_name, user_def in pairs(users) do
        local timeout = user_def.idle_timeout
        if timeout ~= nil then
            assert(timeout >= 0)
            res[user_name] = timeout
        end
    end
    return res
end

local function apply(config_module)
    local configdata = config_module._configdata
    local idle_timeouts = extract_idle_timeouts(configdata)
    box.internal.session.idle_timeout_reset()
    for user_name, timeout in pairs(idle_timeouts) do
        box.internal.session.idle_timeout_set(user_name, timeout)
    end
end

return {
    name = 'session_settings',
    apply = apply,
}
