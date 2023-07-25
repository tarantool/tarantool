local schema = require('internal.config.utils.schema')
local instance_config = require('internal.config.instance_config')

local methods = {}
local mt = {
    __index = methods,
}

-- Gather most actual config values.
function methods.sync(self, _config_module, _iconfig)
    local values = {}

    for _, w in instance_config:pairs() do
        local env_var_name = 'TT_' .. table.concat(w.path, '_'):upper()
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

local function new()
    return setmetatable({
        name = 'env',
        type = 'instance',
        _values = {},
    }, mt)
end

return {
    new = new,
}
