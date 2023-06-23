local fiber = require('fiber')
local log = require('internal.config.utils.log')

local function apply(config)
    local configdata = config._configdata
    local slice = configdata:get('fiber.slice', {use_default = true})
    assert(slice ~= nil)
    log.verbose('fiber.apply: fiber.set_max_slice: %s', slice)
    fiber.set_max_slice(slice)

    local top = configdata:get('fiber.top', {use_default = true})
    assert(type(top) == 'table')
    assert(type(top.enabled) == 'boolean')
    if top.enabled then
        log.verbose('fiber.apply: enable fiber top')
        fiber.top_enable()
    else
        log.verbose('fiber.apply: disable fiber top')
        fiber.top_disable()
    end
end

return {
    name = 'fiber',
    apply = apply,
}
