-- Accumulates configuration data of different kinds and provides
-- accessors.
--
-- Intended to be used as an immutable object.

local fun = require('fun')
local urilib = require('uri')
local digest = require('digest')
local instance_config = require('internal.config.instance_config')
local cluster_config = require('internal.config.cluster_config')
local snapshot = require('internal.config.utils.snapshot')

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
        replicaset_uuid = self._replicaset_uuid,
        instance_uuid = self._instance_uuid,
    }
end

local function uuid_from_name(str)
    local sha = digest.sha1_hex(str)
    return sha:sub(1,8)..'-'..sha:sub(9,12)..'-'..sha:sub(13,16)..'-'..
           '00'..sha:sub(17,18)..'-'..sha:sub(19,30)
end

local function instance_sharding(iconfig, instance_name)
    local roles = instance_config:get(iconfig, 'sharding.roles')
    if roles == nil or #roles == 0 then
        return nil
    end
    assert(type(roles) == 'table')
    local is_storage = false
    for _, role in pairs(roles) do
        is_storage = is_storage or role == 'storage'
    end
    if not is_storage then
        return nil
    end
    local zone = instance_config:get(iconfig, 'sharding.zone')
    local uri = instance_config:instance_uri(iconfig, 'sharding')
    --
    -- Currently, vshard does not accept URI without a username. So if we got a
    -- URI without a username, use "guest" as the username without a password.
    --
    local u, err = urilib.parse(uri)
    -- NB: The URI is validated, so the parsing can't fail.
    assert(u ~= nil, err)
    if u.login == nil then
        u.login = 'guest'
        uri = urilib.format(u, true)
    end
    return {
        uri = uri,
        zone = zone,
        name = instance_name,
    }
end

function methods.sharding(self)
    local sharding = {}
    for group_name, group in pairs(self._cconfig.groups) do
        for replicaset_name, value in pairs(group.replicasets) do
            local lock
            local replicaset_uuid
            local replicaset_cfg = {}
            for instance_name, _ in pairs(value.instances) do
                local vars = {
                    instance_name = instance_name,
                    replicaset_name = replicaset_name,
                    group_name = group_name,
                }
                local iconfig = cluster_config:instantiate(self._cconfig,
                                                           instance_name)
                iconfig = instance_config:apply_default(iconfig)
                iconfig = instance_config:apply_vars(iconfig, vars)
                if lock == nil then
                    lock = instance_config:get(iconfig, 'sharding.lock')
                end
                local isharding = instance_sharding(iconfig, instance_name)
                if isharding ~= nil then
                    if replicaset_uuid == nil then
                        replicaset_uuid = instance_config:get(iconfig,
                            'database.replicaset_uuid')
                        if replicaset_uuid == nil then
                            replicaset_uuid = uuid_from_name(replicaset_name)
                        end
                    end
                    local instance_uuid = instance_config:get(iconfig,
                        'database.instance_uuid')
                    if instance_uuid == nil then
                        instance_uuid = uuid_from_name(instance_name)
                    end
                    replicaset_cfg[instance_uuid] = isharding
                end
            end
            if next(replicaset_cfg) ~= nil then
                assert(replicaset_uuid ~= nil)
                sharding[replicaset_uuid] = {
                    replicas = replicaset_cfg,
                    master = 'auto',
                    lock = lock,
                }
            end
        end
    end
    local cfg = {sharding = sharding}

    local vshard_global_options = {
        'shard_index',
        'bucket_count',
        'rebalancer_disbalance_threshold',
        'rebalancer_max_receiving',
        'rebalancer_max_sending',
        'sync_timeout',
        'connection_outdate_delay',
        'failover_ping_timeout',
        'discovery_mode',
        'sched_ref_quota',
        'sched_move_quota',
    }
    for _, v in pairs(vshard_global_options) do
        cfg[v] = instance_config:get(self._iconfig_def, 'sharding.'..v)
    end
    return cfg
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

function methods.bootstrap_leader(self)
    return self._bootstrap_leader
end

-- Should be called only if the 'supervised' failover method is
-- configured.
function methods.bootstrap_leader_name(self)
    assert(self._failover == 'supervised')
    return self._bootstrap_leader_name
end

-- Returns instance_uuid and replicaset_uuid, saved in config.
local function find_uuids_by_name(peers, instance_name)
    for name, peer in pairs(peers) do
        if name == instance_name then
            local iconfig = peer.iconfig_def
            return instance_config:get(iconfig, 'database.instance_uuid'),
                   instance_config:get(iconfig, 'database.replicaset_uuid')
        end
    end
    return nil
end

local function find_peer_name_by_uuid(peers, instance_uuid)
    for name, peer in pairs(peers) do
        local uuid = instance_config:get(peer.iconfig_def,
                                         'database.instance_uuid')
        if uuid == instance_uuid then
            return name
        end
    end
    return nil
end

function methods.peer_name_by_uuid(self, instance_uuid)
    return find_peer_name_by_uuid(self._peers, instance_uuid)
end

local function find_saved_names(iconfig)
    if type(box.cfg) == 'function' then
        local snap_path = snapshot.get_path(iconfig)
        -- Bootstrap is going to be done, no names are saved.
        if snap_path == nil then
            return nil
        end

        -- Read system spaces of snap file.
        return snapshot.get_names(snap_path)
    end

    -- Box.cfg was already done. No sense in snapshot
    -- reading, we can get all data from memory.
    local peers = {}
    for _, row in ipairs(box.space._cluster:select(nil, {limit = 32})) do
        if row[3] ~= nil then
            peers[row[3]] = row[2]
        end
    end

    return {
        replicaset_name = box.info.replicaset.name,
        replicaset_uuid = box.info.replicaset.uuid,
        instance_name = box.info.name,
        instance_uuid = box.info.uuid,
        peers = peers,
    }
end

-- Return a map, which shows, which instances doesn't have a name
-- set, info about the current replicaset name is also included in map.
function methods.missing_names(self)
    local missing_names = {
        -- Note, that replicaset_name cannot start with underscore (_peers
        -- name is forbidden), so we won't overwrite it with list of peers.
        _peers = {},
    }

    local saved_names = find_saved_names(self._iconfig_def)
    if saved_names == nil then
        -- All names will be set during replicaset bootstrap.
        return missing_names
    end

    -- Missing name of the current replicaset.
    if saved_names.replicaset_name == nil then
        missing_names[self._replicaset_name] = saved_names.replicaset_uuid
    end

    for name, peer in pairs(self._peers) do
        local iconfig = peer.iconfig_def
        -- We allow anonymous replica without instance_uuid. Anonymous replica
        -- cannot have name set, it's enough to validate replicaset_name/uuid.
        if instance_config:get(iconfig, 'replication.anon') then
            goto continue
        end

        -- cfg_uuid may be box.NULL if instance_uuid is not passed to config.
        local cfg_uuid = instance_config:get(iconfig, 'database.instance_uuid')
        if cfg_uuid == box.NULL then
            cfg_uuid = 'unknown'
        end

        if not saved_names.peers[name] then
            missing_names._peers[name] = cfg_uuid
        end

        ::continue::
    end

    return missing_names
end

local mt = {
    __index = methods,
}

-- Validate UUIDs and names passed to config against the data,
-- saved inside snapshot. Fail early if mismatch is found.
local function validate_names(saved_names, config_names)
    -- Snapshot always has replicaset uuid and
    -- at least one peer in _cluster space.
    assert(saved_names.replicaset_uuid)
    assert(saved_names.instance_uuid)
    -- Config always has names set.
    assert(config_names.replicaset_name ~= nil)
    assert(config_names.instance_name ~= nil)

    if config_names.replicaset_uuid ~= nil and
       config_names.replicaset_uuid ~= saved_names.replicaset_uuid then
        error(string.format('Replicaset UUID mismatch. Snapshot: %s, ' ..
                            'config: %s.', saved_names.replicaset_uuid,
                            config_names.replicaset_uuid), 0)
    end

    if saved_names.replicaset_name ~= nil and
       saved_names.replicaset_name ~= config_names.replicaset_name then
        error(string.format('Replicaset name mismatch. Snapshot: %s, ' ..
                            'config: %s.', saved_names.replicaset_name,
                            config_names.replicaset_name), 0)
    end

    if config_names.instance_uuid ~= nil and
       config_names.instance_uuid ~= saved_names.instance_uuid then
        error(string.format('Instance UUID mismatch. Snapshot: %s, ' ..
                            'config: %s.', saved_names.instance_uuid,
                            config_names.instance_uuid), 0)
    end

    if saved_names.instance_name ~= nil and
       saved_names.instance_name ~= config_names.instance_name then
        error(string.format('Instance name mismatch. Snapshot: %s, ' ..
                            'config: %s.', saved_names.instance_name,
                            config_names.instance_name), 0)
    end

    -- Fail early, if current UUID is not set, but no name is found
    -- inside the snapshot file. Ignore this failure, if replica is
    -- configured as anonymous, anon replicas cannot have names.
    local iconfig = config_names.peers[config_names.instance_name].iconfig_def
    if not instance_config:get(iconfig, 'replication.anon') then
        if saved_names.instance_name == nil and
           config_names.instance_uuid == nil then
            error(string.format('Instance name for %s is not set in snapshot' ..
                                ' and UUID is missing in the config. Found ' ..
                                '%s in snapshot.', config_names.instance_name,
                                saved_names.instance_uuid), 0)
        end
        if saved_names.replicaset_name == nil and
           config_names.replicaset_uuid == nil then
            error(string.format('Replicaset name for %s is not set in ' ..
                                'snapshot and  UUID is missing in the ' ..
                                'config. Found %s in snapshot.',
                                config_names.replicaset_name,
                                saved_names.replicaset_uuid), 0)
        end
    end
end

-- A couple of replication.failover specific checks.
local function validate_failover(found, peers, failover, leader)
    if failover ~= 'manual' then
        -- Verify that no leader is set in the "off", "election"
        -- or "supervised" failover mode.
        if leader ~= nil then
            error(('"leader" = %q option is set for replicaset %q of group ' ..
                '%q, but this option cannot be used together with ' ..
                'replication.failover = %q'):format(leader,
                found.replicaset_name, found.group_name, failover), 0)
        end
    end

    if failover ~= 'off' then
        -- Verify that peers in the given replicaset have no direct
        -- database.mode option set if the replicaset is configured
        -- with the "manual", "election" or "supervised" failover
        -- mode.
        --
        -- This check doesn't verify the whole cluster config, only
        -- the given replicaset.
        for peer_name, peer in pairs(peers) do
            local mode = instance_config:get(peer.iconfig, 'database.mode')
            if mode ~= nil then
                error(('database.mode = %q is set for instance %q of ' ..
                    'replicaset %q of group %q, but this option cannot be ' ..
                    'used together with replication.failover = %q'):format(mode,
                    peer_name, found.replicaset_name, found.group_name,
                    failover), 0)
            end
        end
    end

    if failover == 'manual' then
        -- Verify that the 'leader' option is set to a name of an
        -- existing instance from the given replicaset (or unset).
        if leader ~= nil and peers[leader] == nil then
            error(('"leader" = %q option is set for replicaset %q of group ' ..
                '%q, but instance %q is not found in this replicaset'):format(
                leader, found.replicaset_name, found.group_name, leader), 0)
        end
    end

    -- Verify that 'election_mode' option is set to a value other
    -- than 'off' only in the 'failover: election' mode.
    --
    -- The alternative would be silent ignoring the election
    -- mode if failover mode is not 'election'.
    --
    -- For a while, a simple and straightforward approach is
    -- chosen: let the user create an explicit consistent
    -- configuration manually.
    --
    -- We can relax it in a future, though. For example, if two
    -- conflicting options are set in different scopes, we can
    -- ignore one from the outer scope.
    if failover ~= 'election' then
        for peer_name, peer in pairs(peers) do
            local election_mode = instance_config:get(peer.iconfig_def,
                'replication.election_mode')
            if election_mode ~= nil and election_mode ~= 'off' then
                error(('replication.election_mode = %q is set for instance ' ..
                    '%q of replicaset %q of group %q, but this option is ' ..
                    'only applicable if replication.failover = "election"; ' ..
                    'the replicaset is configured with replication.failover ' ..
                    '= %q; if this particular instance requires its own ' ..
                    'election mode, for example, if it is an anonymous ' ..
                    'replica, consider configuring the election mode ' ..
                    'specifically for this particular instance'):format(
                    election_mode, peer_name, found.replicaset_name,
                    found.group_name, failover), 0)
            end
        end
    end
end

local function new(iconfig, cconfig, instance_name)
    -- Find myself in a cluster config, determine peers in the same
    -- replicaset.
    local found = cluster_config:find_instance(cconfig, instance_name)
    assert(found ~= nil)

    -- Precalculate configuration with applied defaults.
    local iconfig_def = instance_config:apply_default(iconfig)

    -- Substitute {{ instance_name }} with actual instance name in
    -- the original config and in the config with defaults.
    --
    -- The same for {{ replicaset_name }} and {{ group_name }}.
    local vars = {
        instance_name = instance_name,
        replicaset_name = found.replicaset_name,
        group_name = found.group_name,
    }
    iconfig = instance_config:apply_vars(iconfig, vars)
    iconfig_def = instance_config:apply_vars(iconfig_def, vars)

    local replicaset_uuid = instance_config:get(iconfig_def,
        'database.replicaset_uuid')
    local instance_uuid = instance_config:get(iconfig_def,
        'database.instance_uuid')
    if replicaset_uuid == nil then
        replicaset_uuid = uuid_from_name(found.replicaset_name)
    end
    if instance_uuid == nil then
        instance_uuid = uuid_from_name(instance_name)
    end

    -- Save instance configs of the peers from the same replicaset.
    local peers = {}
    for peer_name, _ in pairs(found.replicaset.instances) do
        -- Build config for each peer from the cluster config.
        -- Build a config with applied defaults as well.
        local peer_iconfig = cluster_config:instantiate(cconfig, peer_name)
        local peer_iconfig_def = instance_config:apply_default(peer_iconfig)

        -- Substitute variables according to the instance name
        -- of the peer.
        --
        -- The replicaset and group names are same as for the
        -- current instance.
        local peer_vars = {
            instance_name = peer_name,
            replicaset_name = found.replicaset_name,
            group_name = found.group_name,
        }
        peer_iconfig = instance_config:apply_vars(peer_iconfig, peer_vars)
        peer_iconfig_def = instance_config:apply_vars(peer_iconfig_def,
            peer_vars)

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
    validate_failover(found, peers, failover, leader)

    local bootstrap_strategy = instance_config:get(iconfig_def,
        'replication.bootstrap_strategy')
    local bootstrap_leader = found.replicaset.bootstrap_leader
    if bootstrap_strategy ~= 'config' then
        if bootstrap_leader ~= nil then
            error(('The "bootstrap_leader" option cannot be set for '..
                   'replicaset %q because "bootstrap_strategy" for instance '..
                   '%q is not "config"'):format(found.replicaset_name,
                                                instance_name), 0)
        end
    elseif bootstrap_leader == nil then
        error(('The "bootstrap_leader" option cannot be empty for replicaset '..
               '%q because "bootstrap_strategy" for instance %q is '..
               '"config"'):format(found.replicaset_name, instance_name), 0)
    else
        if peers[bootstrap_leader] == nil then
            error(('"bootstrap_leader" = %q option is set for replicaset %q '..
                   'of group %q, but instance %q is not found in this '..
                   'replicaset'):format(bootstrap_leader, found.replicaset_name,
                                        found.group_name, bootstrap_leader), 0)
        end
    end

    -- Verify "replication.failover" = "supervised" strategy
    -- prerequisites.
    local bootstrap_leader_name
    if failover == 'supervised' then
        -- An instance that is potentially a bootstrap leader
        -- starts in RW in assumption that the bootstrap strategy
        -- will choose it as the bootstrap leader.
        --
        -- It doesn't work in at least 'config' and 'supervised'
        -- bootstrap strategies. It is possible to support them,
        -- but an extra logic that is not implemented yet is
        -- required.
        --
        -- See applier/box_cfg.lua for the details.
        if bootstrap_strategy ~= 'auto' then
            error(('"bootstrap_strategy" = %q is set for replicaset %q, but ' ..
                'it is not supported with "replication.failover" = ' ..
                '"supervised"'):format(bootstrap_strategy,
                found.replicaset_name), 0)
        end
        assert(bootstrap_leader == nil)
        bootstrap_leader_name = peer_names[1]
    end

    -- Names and UUIDs are always validated: during instance start
    -- and during config reload.
    local saved_names = find_saved_names(iconfig_def)
    if saved_names ~= nil then
        local config_instance_uuid, config_replicaset_uuid =
            find_uuids_by_name(peers, instance_name)
        validate_names(saved_names, {
            replicaset_name = found.replicaset_name,
            instance_name = instance_name,
            -- UUIDs from config, generated one should not be used here.
            replicaset_uuid = config_replicaset_uuid,
            instance_uuid = config_instance_uuid,
            peers = peers,
        })
    end

    return setmetatable({
        _iconfig = iconfig,
        _iconfig_def = iconfig_def,
        _cconfig = cconfig,
        _peer_names = peer_names,
        _replicaset_uuid = replicaset_uuid,
        _instance_uuid = instance_uuid,
        _peers = peers,
        _group_name = found.group_name,
        _replicaset_name = found.replicaset_name,
        _instance_name = instance_name,
        _failover = failover,
        _leader = leader,
        _bootstrap_leader = bootstrap_leader,
        _bootstrap_leader_name = bootstrap_leader_name,
    }, mt)
end

return {
    new = new,
}
