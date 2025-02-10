local compat = require('compat')
local log = require('internal.config.utils.log')

local function get_effective(option)
    return compat[option]:is_new() and 'new' or 'old'
end

local function validate_config(config)
    local function get_value(option)
        local config_value = config[option]
        local current_value = get_effective(option)
        return config_value ~= nil and config_value or current_value
    end
    if get_value('replication_synchro_timeout') == 'old' and
        get_value('box_begin_timeout_meaning') == 'new' then
        error("Compat options 'replication_synchro_timeout' and " ..
            "'box_begin_timeout_meaning' cannot be set to 'old' " ..
            "and 'new' simultaneously")
    end
end

local function apply(config)
    local configdata = config._configdata
    local compat_options = compat._options()
    local compat_config = configdata:get('compat', {use_default = true})

    validate_config(compat_config)

    for k, v in pairs(compat_config) do
        local is_changed = get_effective(k) ~= v
        if is_changed then
            local def = compat_options[k].default == v and '(default)' or
                '(NOT default)'
            log.info(('Set compat option %q to %q %s'):format(k, v, def))
            compat[k] = v
        end
    end
end

return {
    name = 'compat',
    apply = apply,
}
