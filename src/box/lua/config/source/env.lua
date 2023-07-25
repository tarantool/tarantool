local schema = require('internal.config.utils.schema')
local instance_config = require('internal.config.instance_config')

local methods = {}
local mt = {
    __index = methods,
}

function methods._env_var_name(self, path_in_schema)
    local env_var_name = 'TT_' .. table.concat(path_in_schema, '_'):upper()
    if self._env_var_suffix ~= nil then
        return env_var_name .. self._env_var_suffix
    end
    return env_var_name
end

-- Gather most actual config values.
function methods.sync(self, _config_module, _iconfig)
    local values = {}

    for _, w in instance_config:pairs() do
        local env_var_name = self:_env_var_name(w.path)
        local raw_value = os.getenv(env_var_name)
        local value = schema.fromenv(env_var_name, raw_value, w.schema)
        if value ~= nil then
            instance_config:set(values, w.path, value)
        end
    end

    -- Set the latest config version if unset. This behavior is
    -- specific for the env source.
    if instance_config:get(values, 'config.version') == nil then
        local latest_version = instance_config.schema.config_version
        assert(latest_version ~= nil)
        instance_config:set(values, 'config.version', latest_version)
    end

    self._values = values
end

-- Access the configuration after source:sync().
function methods.get(self)
    return self._values
end

local function new(opts)
    local opts = opts or {}
    local env_var_suffix = opts.env_var_suffix

    local name = 'env'

    if env_var_suffix ~= nil then
        name = ('%s (%s)'):format(name, env_var_suffix)
        env_var_suffix = '_' .. env_var_suffix:upper()
    end

    return setmetatable({
        name = name,
        type = 'instance',

        _values = {},
        _env_var_suffix = env_var_suffix,
    }, mt)
end

return {
    new = new,
}
