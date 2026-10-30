--
-- The net-replicaset recovery point backend. It creates a recovery point on a
-- remote replicaset's leader by calling box.backup.recovery_point.create over
-- net.replicaset.
--
local net_replicaset = require('internal.net.replicaset')
local schema = require('experimental.config.utils.schema')
local enterprise_edition = schema._enterprise_edition

--------------------------------------------------------------------------------
-- Config
--------------------------------------------------------------------------------

local config_schema = schema.new('net-replicaset backend', schema.record({
    target = schema.scalar({type = 'string'}),
    login = schema.scalar({type = 'string'}),
    password = schema.scalar({type = 'string'}),
    params = schema.record({
        transport = schema.enum({'plain', 'ssl'}),
        ssl_key_file = enterprise_edition(schema.scalar({type = 'string'})),
        ssl_cert_file = enterprise_edition(schema.scalar({type = 'string'})),
        ssl_ca_file = enterprise_edition(schema.scalar({type = 'string'})),
        ssl_ciphers = enterprise_edition(schema.scalar({type = 'string'})),
        ssl_password = enterprise_edition(schema.scalar({type = 'string'})),
        ssl_password_file =
            enterprise_edition(schema.scalar({type = 'string'})),
    }),
}, {
    validate = function(data, w)
        if data.target == nil then
            w.error('target is mandatory')
        end
        if data.password ~= nil and data.login == nil then
            w.error('Password cannot be set without setting login')
        end
    end,
}))

local function validate(cfg)
    config_schema:validate(cfg)
end

--------------------------------------------------------------------------------
-- Instance
--------------------------------------------------------------------------------

-- Create a recovery point on the target's leader, tagged with opts.label.
-- Returns the created point, or nil + err on failure (call_leader raises, so
-- wrap it) -- the manager retries.
local function instance_create_point(instance, opts)
    local ok, res = pcall(instance.rs.call_leader, instance.rs,
                          'box.backup.recovery_point.create',
                          {{label = opts.label}}, {timeout = opts.timeout})
    if not ok then
        return nil, res
    end
    return res
end

local function instance_drop(instance)
    instance.rs:close()
end

local function instance_info(instance)
    local info = instance.rs:info()
    info.backend_type = 'net-replicaset'
    return info
end

local instance_mt = {
    __index = {
        create_point = instance_create_point,
        drop = instance_drop,
        info = instance_info,
    },
}

local function new(cfg)
    -- The target replicaset name is resolved through the declarative config.
    if box.info.config.status == 'uninitialized' then
        error('config is uninitialized')
    end
    local connect_cfg = net_replicaset.get_connect_cfg(cfg.target, {
        login = cfg.login,
        password = cfg.password,
        params = cfg.params,
    })
    return setmetatable({rs = net_replicaset.connect(connect_cfg)}, instance_mt)
end

--------------------------------------------------------------------------------
-- Module
--------------------------------------------------------------------------------

return {
    config = {
        validate = validate,
    },
    new = new,
}
