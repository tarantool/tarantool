local expression = require('internal.config.utils.expression')
local access_control = require('access_control')

--
-- Create a credential sharding role if it is assigned to any user or role and
-- has not been declared in 'credentials.roles'.
--
-- Replicasets with the sharding storage role must be assigned the credentials
-- sharding role. In these replicasets, the role will be assigned all necessary
-- privileges.
--
-- For replicasets that do not have the sharding storage role assigned, the
-- credentials sharding role will not have any privileges.
--
-- If the snapshot version does not match the Tarantool schema version, DDL
-- operations, including creating roles and granting privileges, are prohibited.
-- For user-created changes, this will throw an error as expected, but since the
-- "sharding" role is created by default, the error may still appear even if the
-- user does not make any changes. To partially avoid this problem, we do not
-- create the "sharding" role if it is not in use. This is a temporary solution.
local function sharding_role(config)
    local configdata = config._configdata
    local roles = configdata:get('credentials.roles')
    local users = configdata:get('credentials.users')
    local has_sharding_role = false
    for _, role in pairs(roles or {}) do
        for _, role_role in pairs(role.roles or {}) do
            has_sharding_role = has_sharding_role or role_role == 'sharding'
        end
    end
    for _, user in pairs(users or {}) do
        for _, user_role in pairs(user.roles or {}) do
            has_sharding_role = has_sharding_role or user_role == 'sharding'
        end
    end
    if not has_sharding_role then
        return
    end

    local credentials = {roles = {sharding = {}}}
    -- Add necessary privileges if storage sharding role is enabled.
    local sharding_roles = configdata:get('sharding.roles')
    if sharding_roles == nil or #sharding_roles == 0 then
        return credentials
    end
    local is_storage = false
    for _, role in pairs(sharding_roles) do
        is_storage = is_storage or role == 'storage'
    end
    if not is_storage then
        return credentials
    end

    local funcs = {}
    --
    -- The error will be thrown later, in sharding.lua. Here we are simply
    -- trying to avoid the "module not found" error.
    --
    local ok, vshard = pcall(require, 'vshard')
    if ok and expression.eval('v >= 0.1.25', {v = vshard.consts.VERSION}) then
        local vexports = require('vshard.storage.exports')
        local exports = vexports.compile(vexports.log[#vexports.log])
        for name in pairs(exports.funcs) do
            table.insert(funcs, name)
        end
    end
    credentials.roles.sharding = {
        privileges = {{
            permissions = {'execute'},
            functions = funcs,
        }},
        roles = {'replication'},
    }
    return credentials
end

local function apply(config_module)
    access_control._set_aboard(config_module._aboard)
    access_control.set('config', config_module._configdata:get('credentials'))
    local sharding_credentials = sharding_role(config_module)
    if sharding_credentials ~= nil then
        access_control.set('sharding', sharding_credentials)
    end
end

return {
    name = 'credentials',
    apply = apply,
}
