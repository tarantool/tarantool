local yaml = require('yaml')
local log = require('internal.config.utils.log')
local cluster_config = require('internal.config.cluster_config')
local instance_config = require('internal.config.instance_config')

local methods = {}
local mt = {
    __index = methods,
}

function methods.sync(self, _config_module, iconfig)
    local roles = instance_config:get(iconfig, 'roles')
    if roles == nil then
        return
    end

    local res = {}
    for _, role_name in ipairs(roles) do
        local ok, ret = pcall(require, role_name)
        if ok and ret.credentials ~= nil then
            log.info(('Fetch credentials from role %q'):format(role_name))

            local credentials = ret.credentials
            if type(credentials) == 'string' then
                credentials = yaml.decode(credentials)
            end

            local config = {credentials = credentials}
            ok, ret = pcall(cluster_config.validate, cluster_config, config)
            if not ok then
                error(('credentials from roles: invalid credentials in role ' ..
                       '%q: %s'):format(role_name, ret), 0)
            end

            local cconfig = cluster_config:apply_conditional(config)
            res = cluster_config:merge(res, cconfig)
        end
    end

    self._values = res
end

function methods.get(self)
    return self._values
end

local function new()
    return setmetatable({
        name = 'roles',
        type = 'cluster',
        _values = {},
    }, mt)
end

return {
    new = new,
}
