local compat = require('compat')

local function apply(config)
    local configdata = config._configdata
    compat(configdata:get('compat', {use_default = true}))
end

return {
    name = 'compat',
    apply = apply,
}
