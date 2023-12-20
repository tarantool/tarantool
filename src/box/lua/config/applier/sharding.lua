local log = require('internal.config.utils.log')
_G.vshard = nil

local function apply(config)
    local configdata = config._configdata
    local roles = configdata:get('sharding.roles')
    if roles == nil then
        return
    end
    if _G.vshard == nil then
        _G.vshard = require('vshard')
    end
    local is_storage = false
    local is_router = false
    for _, role in pairs(roles) do
        if role == 'storage' then
            is_storage = true
        elseif role == 'router' then
            is_router = true
        end
    end
    local cfg = configdata:sharding()
    if is_storage then
        log.info('sharding: apply storage config')
        -- Name may be not set in box.info.name, e.g. during names applying.
        -- Configure vshard anyway, pass configuration name.
        _G.vshard.storage.cfg(cfg, configdata:names().instance_name)
    end
    if is_router then
        log.info('sharding: apply router config')
        _G.vshard.router.cfg(cfg)
    end
end

return {
    name = 'sharding',
    apply = apply,
}
