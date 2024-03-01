local access_control = require('access_control')

local function apply(config_module)
    access_control._set_aboard(config_module._aboard)
    access_control.set('config', config_module._configdata:get('credentials'))
end

return {
    name = 'credentials',
    apply = apply,
}
