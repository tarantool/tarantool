local expression = require('internal.config.utils.expression')
local log = require('internal.config.utils.log')
_G.vshard = nil

-- Watcher which will create all the necessary functions.
local watcher = nil

local function apply(config)
    local configdata = config._configdata
    local roles = configdata:get('sharding.roles')
    if roles == nil then
        return
    end
    -- Make sure vshard is available and its version is not too old.
    local ok, vshard = pcall(require, 'vshard')
    if not ok then
        error('The vshard module is not available', 0)
    end
    if expression.eval('v < 0.1.25', {v = vshard.consts.VERSION}) then
        error('The vshard module is too old: the minimum supported version ' ..
              'is 0.1.25.', 0)
    end

    _G.vshard = vshard
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
                local vexports = require('vshard.storage.exports')
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
