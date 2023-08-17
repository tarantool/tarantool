local log = require('internal.config.utils.log')
local fiber = require('fiber')
_G.vshard = nil

local fiber_wait_ro_rw

local function vshard_cfg(config)
    local configdata = config._configdata
    local roles = configdata:get('sharding.roles')
    if roles == nil then
        return
    end
    if _G.vshard == nil then
        _G.vshard = require('vshard')
    end
    for _, role in pairs(roles) do
        local cfg = configdata:sharding()
        --
        -- Make vshard repeat current box.cfg options (see vshard/issues/428).
        -- TODO: delete when box.cfg{} will not be called in vshard.
        --
        cfg.listen = box.cfg.listen
        cfg.read_only = box.cfg.read_only
        cfg.replication = box.cfg.replication

        if role == 'storage' then
            local names = configdata:names()
            local replicaset_uuid = names.replicaset_uuid
            assert(replicaset_uuid == box.cfg.replicaset_uuid)
            local instance_uuid = names.instance_uuid
            assert(instance_uuid == box.cfg.instance_uuid)
            local this_replicaset_cfg = cfg.sharding[replicaset_uuid]
            --
            -- Currently, the replicaset master must set itself as the master in
            -- its own configuration.
            -- TODO: remove when vshard introduces auto-discovery of masters.
            --
            if not box.info.ro then
                this_replicaset_cfg.master = nil
                this_replicaset_cfg.replicas[instance_uuid].master = true
            end
            log.info('sharding: apply sharding config')
            _G.vshard.storage.cfg(cfg, instance_uuid)
            --
            -- Currently, replicaset masters may not be aware of all other
            -- masters, so the rebalancer is disabled.
            -- TODO: remove when vshard introduces auto-discovery of masters.
            --
            if _G.vshard.storage.internal.is_rebalancer_active then
                log.info('sharding: disable rebalancer')
                _G.vshard.storage.rebalancer_disable()
            end
        end
        if role == 'router' then
            _G.vshard.router.cfg(cfg)
        end
    end
end

local function wait_ro_rw(config)
    while true do
        if box.info.ro then
            box.ctl.wait_rw()
        else
            box.ctl.wait_ro()
        end
        local ok, err = pcall(vshard_cfg, config)
        if not ok then
            log.error(err)
        end
    end
end

local function apply(config)
    vshard_cfg(config)
    if fiber_wait_ro_rw == nil then
        fiber_wait_ro_rw = fiber.create(wait_ro_rw, config)
    end
end

return {
    name = 'sharding',
    apply = apply,
}
