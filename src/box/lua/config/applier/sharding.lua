local log = require('internal.config.utils.log')
local loaders = require('internal.loaders')
_G.vshard = nil

-- Watcher which will create all the necessary functions.
local watcher = nil

local function apply(config)
    local configdata = config._configdata
    local roles = configdata:get('sharding.roles')
    if roles == nil then
        return
    end
    -- VShard availability and its minimum version are ensured by the sharding
    -- configuration validation (the `vshard_since` schema annotation).
    _G.vshard = loaders.require_first('vshard-ee', 'vshard')
    assert(_G.vshard)
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
        -- Start a watcher which will create all the necessary functions.
        if watcher == nil then
            local function deploy_funcs()
                local vexports = loaders.require_first(
                    'vshard-ee.storage.exports', 'vshard.storage.exports')
                local exports = vexports.compile(vexports.log[#vexports.log])
                vexports.deploy_funcs(exports)
            end
            watcher = box.watch('box.status', function(_, status)
                -- It's ok, if deploy_funcs() will be triggered several times.
                if status.is_ro == false then
                    deploy_funcs()
                end
            end)
        end
        log.info('sharding: apply storage config')
        -- Name may be not set in box.info.name, e.g. during names applying.
        -- Configure vshard anyway, pass configuration name.
        _G.vshard.storage.cfg(cfg, configdata:names().instance_name)
    elseif watcher ~= nil then
        watcher:unregister()
        watcher = nil
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
