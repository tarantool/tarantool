local tarantool = require('tarantool')
local urilib = require('uri')
local uuid = require('uuid')
local network = require('internal.config.utils.network')


-- Store validation functions. Each key in this table corresponds
-- to a specific schema field path, and the associated value is a
-- function that validates the data for that field.
local M = {}

local function validate_uuid_str(data, w)
    if uuid.fromstr(data) == nil then
        w.error('Unable to parse the value as a UUID: %q', data)
    end
    if data == uuid.NULL:str() then
        w.error('nil UUID is reserved')
    end
end

-- Verify 'uri' field in iproto.listen and iproto.advertise.* options.
local function validate_uri_field(has_login_field, used_to_connect)
    return function(data, w)
        -- Substitute variables with placeholders to don't confuse
        -- the URI parser with the curly brackets.
        data = data:gsub('{{ *.- *}}', 'placeholder')
        local uri, err = urilib.parse(data)
        if uri == nil then
            if data:find(',') then
                w.error('A single URI is expected, not a list of URIs')
            end
            w.error('Unable to parse an URI: %s', err)
        end
        if uri.login ~= nil and has_login_field then
            w.error("Login must be set via the 'login' option")
        end
        if uri.login ~= nil and not has_login_field then
            w.error('Login cannot be set for as part of the URI')
        end
        assert(uri.password == nil)
        if uri.params ~= nil then
            w.error("URI parameters should be described in the 'params' " ..
                "field, not as the part of URI")
        end
        if used_to_connect then
            local ok, err = network.uri_is_suitable_to_connect(uri)
            if not ok then
                w.error("bad URI %q: %s", data, err)
            end
        end
    end
end

local function feedback_validate(data, w)
    if data == nil or box.internal.feedback_daemon ~= nil then
        return
    end
    w.error('Tarantool is built without feedback reports sending support')
end

-- Verify that the given validation context (w) corresponds to one
-- of the given cluster config scopes (global, group, replicaset,
-- instance) or the validation is performed against the instance
-- config schema.
local function validate_scope(w, allowed_scopes)
    local scope = w.schema.computed.annotations.scope

    -- scope = nil means that the validation is performed against
    -- the instance config schema, not the cluster config.
    --
    -- The validation against the instance config is performed for
    -- values from the env configuration source (which collects
    -- the TT_* environment variables).
    --
    -- Also, it would be very counter-intuitive if
    -- cluster_config:instantiate() would produce a result that
    -- doesn't pass the instance_config:validate() check.
    if scope == nil then
        return
    end

    -- If the current scope is listed in the allowed scopes -- OK.
    for _, allowed_scope in ipairs(allowed_scopes) do
        if scope == allowed_scope then
            return
        end
    end

    -- Any other level of the cluster configuration -- raise an
    -- error.
    w.error('The option must not be present in the %s scope', scope)
end

-- {{{ app

M['app'] = function(app, w)
    if app.file ~= nil and app.module ~= nil then
        w.error('Fields file and module cannot appear at the same time')
    end
end

-- }}} app

-- {{{ config

M['config.context.*'] = function(var, w)
    if var.from == nil then
        w.error('"from" field must be defined in a context ' ..
            'variable definition')
    end
    if var.from == 'env' and var.env == nil then
        w.error('"env" field must define an environment ' ..
            'variable name if "from" field is set to "env"')
    end
    if var.from == 'file' and var.file == nil then
        w.error('"file" field must define a file name if ' ..
            '"from" field is set to "file"')
    end
end

M['config.etcd'] = function(data, w)
    -- No config.etcd section at all -- OK.
    if data == nil or next(data) == nil then
        return
    end
    -- There is some data -- the prefix should be there.
    if data.prefix == nil then
        w.error('No config.etcd.prefix provided')
    end
end

M['config.etcd.prefix'] = function(data, w)
    if not data:startswith('/') then
        w.error(('config.etcd.prefix should be a path alike ' ..
            'value, got %q'):format(data))
    end
end

M['config.storage'] = function(data, w)
    if data == nil or next(data) == nil then
        return
    end
    if data.prefix == nil and data.endpoints == nil then
        return
    end
    if data.prefix == nil then
        w.error('No config.storage.prefix provided')
    end
    if data.endpoints == nil then
        w.error('No config.storage.endpoints provided')
    end
end

M['config.storage.endpoints'] = function(data, w)
    if #data == 0 then
        w.error('At least one endpoint must be' ..
            'specified in config.storage.endpoints')
    end
end

M['config.storage.prefix'] = function(data, w)
    if not data:startswith('/') then
        w.error(('config.storage.prefix should be ' ..
            'a path alike value, got %q'):format(data))
    end
end

-- }}} config

-- {{{ database

M['database.instance_uuid'] = validate_uuid_str

M['database.replicaset_uuid'] = validate_uuid_str

-- }}} database

-- {{{ failover

M['failover'] = function(_data, w)
    validate_scope(w, {'global'})
end

M['failover.log'] = function(data, w)
    if data.to == 'file' and data.file == nil then
        w.error('log.file must be specified when log.to is "file"')
    end
end

-- }}} failover

-- {{{ feedback

M['feedback.crashinfo'] = feedback_validate

M['feedback.enabled'] = feedback_validate

M['feedback.host'] = feedback_validate

M['feedback.interval'] = feedback_validate

M['feedback.metrics_collect_interval'] = feedback_validate

M['feedback.metrics_limit'] = feedback_validate

M['feedback.send_metrics'] = feedback_validate

-- }}} feedback

-- {{{ iproto

M['iproto.advertise.client'] = validate_uri_field(false, true)

M['iproto.advertise.peer'] = function(data, w)
    if next(data) == nil then
        w.error('An URI should have at least one field')
    end
    -- If a password is set, a login must also be specified.
    if data.password ~= nil and data.login == nil then
        w.error('Password cannot be set without setting login')
    end
    -- If a params is set, an uri must also be specified.
    if data.params ~= nil and data.uri == nil then
        w.error('Params cannot be set without setting uri')
    end
end

M['iproto.advertise.peer.uri'] = validate_uri_field(true, true)

M['iproto.advertise.sharding'] = function(data, w)
    if next(data) == nil then
        w.error('An URI should have at least one field')
    end
    -- If a password is set, a login must also be specified.
    if data.password ~= nil and data.login == nil then
        w.error('Password cannot be set without setting login')
    end
    -- If a params is set, an uri must also be specified.
    if data.params ~= nil and data.uri == nil then
        w.error('Params cannot be set without setting uri')
    end
end

M['iproto.advertise.sharding.uri'] = validate_uri_field(true, true)

M['iproto.listen.*'] = function(data, w)
    -- If data is not nil then the URI should be there.
    if data.uri == nil then
        w.error('The URI is required for iproto.listen')
    end
end

M['iproto.listen.*.uri'] = validate_uri_field(false, false)

-- }}} iproto

-- {{{ isolated

M['isolated'] = function(_data, w)
    validate_scope(w, {'instance'})
end

-- }}} isolated

-- {{{ log

M['log'] = function(log, w)
    if log.to == 'pipe' and log.pipe == nil then
        w.error('The pipe logger is set by the log.to parameter but ' ..
            'the command is not set (log.pipe parameter)')
    end
end

-- }}} log

-- {{{ lua

M['lua.memory'] = function(data, w)
    if data < 256 * 1024 * 1024 then
        w.error('Memory limit should be >= 256MB')
    end
end

-- }}} lua

-- {{{ replication

M['replication.autoexpel'] = function(data, w)
    -- Forbid in the instance scope, because it has no
    -- sense to set the option for a part of a
    -- replicaset.
    validate_scope(w, {'global', 'group', 'replicaset'})

    -- Don't validate the options if this
    -- functionality is disabled.
    if not data.enabled then
        return
    end

    -- If autoexpelling is enabled, the expelling
    -- criterion must be set.
    if data.by == nil then
        w.error('replication.autoexpel.by must be set if ' ..
            'replication.autoexpel.enabled = true')
    end

    -- If the autoexpelling is configured to use the
    -- prefix-based criterion, then the prefix must be
    -- set.
    if data.by == 'prefix' and data.prefix == nil then
        w.error('replication.autoexpel.prefix must be set if ' ..
            'replication.autoexpel.enabled = true and ' ..
            'replication.autoexpel.by = \'prefix\'')
    end
end

M['replication.failover'] = function(_data, w)
    -- Forbid in the instance scope, because it has no
    -- sense to set the option for a part of a
    -- replicaset.
    validate_scope(w, {'global', 'group', 'replicaset'})
end

-- }}} replication

-- {{{ security

M['security.auth_type'] = function(auth_type, w)
    if auth_type ~= 'chap-sha1' and
            tarantool.package ~= 'Tarantool Enterprise' then
        w.error('"chap-sha1" is the only authentication method ' ..
                '(auth_type) available in Tarantool Community ' ..
                'Edition (%q requested)', auth_type)
    end
end

-- }}} security

-- {{{ sharding

M['sharding'] = function(data, w)
    -- Forbid sharding.roles in instance scope.
    local scope = w.schema.computed.annotations.scope
    if data.roles ~= nil and scope == 'instance' then
        w.error('sharding.roles cannot be defined in the instance ' ..
                'scope')
    end
    -- Make sure that if the rebalancer role is present, the storage
    -- role is also present.
    if data.roles ~= nil then
        local has_storage = false
        local has_rebalancer = false
        for _, role in pairs(data.roles) do
            has_storage = has_storage or role == 'storage'
            has_rebalancer = has_rebalancer or role == 'rebalancer'
        end
        if has_rebalancer and not has_storage then
            w.error('The rebalancer role cannot be present without ' ..
                    'the storage role')
        end
    end
end

M['sharding.bucket_count'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.bucket_count should be a defined in '..
                'global scope')
    end
end

M['sharding.connection_outdate_delay'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.connection_outdate_delay should be a '..
                'defined in global scope')
    end
end

M['sharding.discovery_mode'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.discovery_mode should be a defined in '..
                'global scope')
    end
end

M['sharding.failover_ping_timeout'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.failover_ping_timeout should be a '..
                'defined in global scope')
    end
end

M['sharding.lock'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope == 'instance' then
        w.error('sharding.lock cannot be defined in the instance '..
                'scope')
    end
end

M['sharding.rebalancer_disbalance_threshold'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.rebalancer_disbalance_threshold should '..
                'be a defined in global scope')
    end
end

M['sharding.rebalancer_max_receiving'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.rebalancer_max_receiving should '..
                'be a defined in global scope')
    end
end

M['sharding.rebalancer_max_sending'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.rebalancer_max_sending should '..
                'be a defined in global scope')
    end
end

M['sharding.rebalancer_mode'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.rebalancer_enabled must be defined in ' ..
                'the global scope.')
    end
end

M['sharding.sched_move_quota'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.sched_move_quota should be a defined ' ..
                'in global scope')
    end
end

M['sharding.sched_ref_quota'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.sched_ref_quota should be a defined ' ..
                'in global scope')
    end
end

M['sharding.shard_index'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.shard_index should be a defined in '..
                'global scope')
    end
end

M['sharding.sync_timeout'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope ~= 'global' then
        w.error('sharding.sync_timeout should be a defined in '..
                'global scope')
    end
end

M['sharding.weight'] = function(data, w)
    local scope = w.schema.computed.annotations.scope
    if data == nil or scope == nil then
        return
    end
    if scope == 'instance' then
        w.error('sharding.weight cannot be defined in the ' ..
                'instance scope')
    end
end

-- }}} sharding

return M
