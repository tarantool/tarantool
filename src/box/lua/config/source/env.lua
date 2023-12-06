local schema = require('internal.config.utils.schema')
local tabulate = require('internal.config.utils.tabulate')
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

function methods._env_list(self)
    local rows = {}

    local ce = 'Community Edition'
    local ee = 'Enterprise Edition'

    -- A header of the table.
    table.insert(rows, {
        'ENVIRONMENT VARIABLE',
        'TYPE',
        'DEFAULT',
        'AVAILABILITY',
    })
    table.insert(rows, tabulate.SPACER)

    -- Environment variables that duplicate CLI options.
    table.insert(rows, {
        'TT_INSTANCE_NAME',
        'string',
        'N/A',
        ce,
    })
    table.insert(rows, {
        'TT_CONFIG',
        'string',
        'nil',
        ce,
    })
    table.insert(rows, tabulate.SPACER)

    -- Collect the options from the schema and sort them
    -- lexicographically.
    local options = instance_config:pairs():totable()
    table.sort(options, function(a, b)
        return table.concat(a.path, '_') < table.concat(b.path, '_')
    end)

    -- Transform the schema nodes (options) into the environment
    -- variables description.
    for _, w in ipairs(options) do
        local default = w.schema.default
        if default == nil and type(default) == 'cdata' then
            default = 'box.NULL'
        end

        table.insert(rows, {
            self._env_var_name(self, w.path),
            w.schema.type,
            tostring(default),
            w.schema.computed.annotations.enterprise_edition and ee or ce,
        })
    end

    return tabulate.encode(rows)
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
