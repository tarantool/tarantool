local expression = require('internal.config.utils.expression')
local health = require('internal.healthcheck')
local log = require('internal.config.utils.log')
local loaders = require('internal.loaders')
_G.vshard = nil

-- Watcher which will create all the necessary functions.
local watcher = nil

local function unregister_vshard_health_checks()
    health.remove_health_check('vshard.router', {if_exists = true})
end

local function register_vshard_router_health_check()
    unregister_vshard_health_checks()
    local ok, err = health.add_health_check('vshard.router', function()
        local info = _G.vshard.router.info()
        local bucket = info and info.bucket
        if bucket == nil or bucket.unknown == nil then
            return false, 'bucket discovery status is unknown'
        end
        if bucket.unknown > 0 then
            return false, 'buckets are not discovered yet'
        end
        return true
    end)
    if not ok then
        error(('Failed to register vshard.router health check: %s'):format(
            err), 0)
    end
end

-- Whether the storage and the router vshard roles are configured
-- on the instance.
local function get_vshard_roles(configdata)
    local is_storage = false
    local is_router = false
    for _, role in pairs(configdata:get('sharding.roles') or {}) do
        if role == 'storage' then
            is_storage = true
        elseif role == 'router' then
            is_router = true
        end
    end
    return is_storage, is_router
end

-- Make sure vshard is available and its version is not too old.
--
-- The check is performed before box.cfg() and so before a
-- potentially long database recovery: a misconfiguration is
-- reported as fast as possible.
--
-- It can't be done even earlier, on the configuration validation
-- stage, for two reasons. First, the validation is performed
-- before the module search root is pointed to process.work_dir
-- (the work_dir is not known yet), so a module installed into the
-- working directory is not resolvable at that point. Second, the
-- same schema validates cluster configurations as a whole, while
-- the module is only needed on instances that have a sharding
-- role.
local function check_vshard(config)
    local configdata = config._configdata
    local is_storage, is_router = get_vshard_roles(configdata)
    if not is_storage and not is_router then
        return
    end
    local ok, vshard = pcall(loaders.require_first, 'vshard-ee', 'vshard')
    if not ok then
        error('The vshard-ee/vshard module is not available', 0)
    end
    local version = vshard.consts.VERSION
    -- The minimum vshard version accepted by the sharding section
    -- and by particular sharding options is declared in the schema,
    -- see the vshard_since annotation. Verify the configured ones.
    configdata:filter(function(w)
        return w.schema.vshard_since ~= nil
    end):each(function(w)
        if w.data == nil then
            return
        end
        local since = w.schema.vshard_since
        if expression.eval('v < ' .. since, {v = version}) then
            error(('%s: The vshard module is too old: the minimum ' ..
                   'supported version is %s'):format(
                   table.concat(w.path, '.'), since), 0)
        end
    end)
end

local function apply(config)
    local configdata = config._configdata
    local is_storage, is_router = get_vshard_roles(configdata)
    if not is_storage and not is_router then
        unregister_vshard_health_checks()
        return
    end
    -- The availability and the minimum version are verified by
    -- the sharding.stage_1 applier.
    _G.vshard = loaders.require_first('vshard-ee', 'vshard')
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
        register_vshard_router_health_check()
    else
        unregister_vshard_health_checks()
    end
end

return {
    stage_1 = {
        name = 'sharding.stage_1',
        apply = check_vshard,
    },
    stage_2 = {
        name = 'sharding.stage_2',
        apply = apply,
    },
}
