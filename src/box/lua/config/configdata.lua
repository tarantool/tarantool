-- Accumulates configuration data of different kinds and provides
-- accessors.
--
-- Intended to be used as an immutable object.

local fun = require('fun')
local instance_config = require('internal.config.instance_config')
local cluster_config = require('internal.config.cluster_config')

local function choose_iconfig(self, opts)
    if opts ~= nil and opts.peer ~= nil then
        local peers = self._peers
        local peer = peers[opts.peer]
        if peer == nil then
            error(('Unknown peer %q'):format(opts.peer), 0)
        end
        if opts ~= nil and opts.use_default then
            return peer.iconfig_def
        end
        return peer.iconfig
    end

    if opts ~= nil and opts.use_default then
        return self._iconfig_def
    else
        return self._iconfig
    end
end

local methods = {}

-- Acquire a value from the instance config.
--
-- opts:
--     use_default: boolean
--     peer: string
function methods.get(self, path, opts)
    local data = choose_iconfig(self, opts)
    return instance_config:get(data, path)
end

-- Filter data based on the instance schema annotations.
--
-- opts:
--     use_default: boolean
--     peer: string
function methods.filter(self, f, opts)
    local data = choose_iconfig(self, opts)
    return instance_config:filter(data, f)
end

-- List of names of the instances in the same replicaset.
--
-- The names are useful to pass to other methods as opts.peer.
function methods.peers(self)
    return self._peer_names
end

-- Group, replicaset and instance names.
function methods.names(self)
    return {
        group_name = self._group_name,
        replicaset_name = self._replicaset_name,
        instance_name = self._instance_name,
    }
end

-- Should be called only if the 'manual' failover method is
-- configured.
function methods.leader(self)
    assert(self._failover == 'manual')
    return self._leader
end

-- Should be called only if the 'manual' failover method is
-- configured.
function methods.is_leader(self)
    assert(self._failover == 'manual')
    return self._leader == self._instance_name
end

local mt = {
    __index = methods,
}

local function apply_vars_f(data, w, vars)
    if w.schema.type == 'string' and data ~= nil then
        assert(type(data) == 'string')
        return (data:gsub('{{ *(.-) *}}', function(var_name)
            if vars[var_name] ~= nil then
                return vars[var_name]
            end
            w.error(('Unknown variable %q'):format(var_name))
        end))
    end
    return data
end

local function iconfig_apply_vars(iconfig, vars)
    return instance_config:map(iconfig, apply_vars_f, vars)
end

local function new(iconfig, cconfig, instance_name)
    -- Precalculate configuration with applied defaults.
    local iconfig_def = instance_config:apply_default(iconfig)

    -- Substitute {{ instance_name }} with actual instance name in
    -- the original config and in the config with defaults.
    local vars = {instance_name = instance_name}
    iconfig = iconfig_apply_vars(iconfig, vars)
    iconfig_def = iconfig_apply_vars(iconfig_def, vars)

    -- Find myself in a cluster config, determine peers in the same
    -- replicaset.
    local found = cluster_config:find_instance(cconfig, instance_name)
    assert(found ~= nil)

    -- Save instance configs of the peers from the same replicaset.
    local peers = {}
    for peer_name, _ in pairs(found.replicaset.instances) do
        -- Build config for each peer from the cluster config.
        -- Build a config with applied defaults as well.
        local peer_iconfig = cluster_config:instantiate(cconfig, peer_name)
        local peer_iconfig_def = instance_config:apply_default(peer_iconfig)

        -- Substitute variables according to the instance name
        -- of the peer.
        local peer_vars = {instance_name = peer_name}
        peer_iconfig = iconfig_apply_vars(peer_iconfig, peer_vars)
        peer_iconfig_def = iconfig_apply_vars(peer_iconfig_def, peer_vars)

        peers[peer_name] = {
            iconfig = peer_iconfig,
            iconfig_def = peer_iconfig_def,
        }
    end

    -- Make the order of the peers predictable and the same on all
    -- instances in the replicaset.
    local peer_names = fun.iter(peers):totable()
    table.sort(peer_names)

    -- The replication.failover option is forbidden for the
    -- instance scope of the cluster config, so it is common for
    -- the whole replicaset. We can extract it from the
    -- configuration of the given instance.
    --
    -- There is a nuance: the option still can be set using an
    -- environment variable. We can't detect incorrect usage in
    -- this case (say, different failover modes for different
    -- instances in the same replicaset), because we have no
    -- access to environment of other instances.
    local failover = instance_config:get(iconfig_def, 'replication.failover')
    local leader = found.replicaset.leader

    if failover == 'off' then
        -- Verify that no leader is set in this mode.
        if leader ~= nil then
            error(('"leader" = %q option is set for replicaset %q of group ' ..
                '%q, but this option cannot be used together with ' ..
                'replication.failover = "off"'):format(leader,
                found.replicaset_name, found.group_name), 0)
        end
    elseif failover == 'manual' then
        -- Verify that peers in the given replicaset have no direct
        -- database.mode option set if the replicaset is configured
        -- with the manual failover mode.
        --
        -- This check doesn't verify the whole cluster config, only
        -- the given replicaset.
        for peer_name, peer in pairs(peers) do
            local mode = instance_config:get(peer.iconfig, 'database.mode')
            if mode ~= nil then
                error(('database.mode = %q is set for instance %q of ' ..
                    'replicaset %q of group %q, but this option cannot be ' ..
                    'used together with replication.failover = ' ..
                    '"manual"'):format(mode, peer_name, found.replicaset_name,
                    found.group_name), 0)
            end
        end

        -- Verify that the 'leader' option is set to a name of an
        -- existing instance from the given replicaset (or unset).
        if leader ~= nil and peers[leader] == nil then
            error(('"leader" = %q option is set for replicaset %q of group ' ..
                '%q, but instance %q is not found in this replicaset'):format(
                leader, found.replicaset_name, found.group_name, leader), 0)
        end
    end

    return setmetatable({
        _iconfig = iconfig,
        _iconfig_def = iconfig_def,
        _cconfig = cconfig,
        _peer_names = peer_names,
        _peers = peers,
        _group_name = found.group_name,
        _replicaset_name = found.replicaset_name,
        _instance_name = instance_name,
        _failover = failover,
        _leader = leader,
    }, mt)
end

return {
    new = new,
}
