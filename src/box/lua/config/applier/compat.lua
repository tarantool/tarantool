local compat = require('compat')
local log = require('internal.config.utils.log')

local function apply(config)
    local configdata = config._configdata
    local compat_options = compat._options()

    for k, v in pairs(configdata:get('compat', {use_default = true})) do
        local effective_value = compat[k]:is_new() and 'new' or 'old'
        local is_changed = effective_value ~= v
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
