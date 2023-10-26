local fiber = require('fiber')
local log = require('internal.config.utils.log')
local instance_config = require('internal.config.instance_config')
local snapshot = require('internal.config.utils.snapshot')

local function peer_uri(configdata, peer_name)
    local iconfig = configdata._peers[peer_name].iconfig_def
    return instance_config:instance_uri(iconfig, 'peer')
end

local function peer_uris(configdata)
    local peers = configdata:peers()
    if #peers <= 1 then
        return nil
    end

    local names = configdata:names()
    local err_msg_prefix = ('replication.peers construction for instance %q ' ..
        'of replicaset %q of group %q'):format(names.instance_name,
        names.replicaset_name, names.group_name)

    -- Is there a peer in our replicaset with an URI suitable to
    -- connect (except ourself)?
    local has_upstream = false

    local uris = {}
    for _, peer_name in ipairs(peers) do
        local uri = peer_uri(configdata, peer_name)
        if uri == nil then
            log.info('%s: instance %q has no iproto.advertise.peer or ' ..
                'iproto.listen URI suitable to create a client socket',
                err_msg_prefix, peer_name)
        end
        if uri ~= nil and peer_name ~= names.instance_name then
            has_upstream = true
        end
        table.insert(uris, uri)
    end

    if not has_upstream then
        error(('%s: no suitable peer URIs found'):format(err_msg_prefix), 0)
    end

    return uris
end

local function log_destination(log)
    if log.to == 'stderr' or log.to == 'devnull' then
        return box.NULL
    elseif log.to == 'file' then
        return ('file:%s'):format(log.file)
    elseif log.to == 'pipe' then
        return ('pipe:%s'):format(log.pipe)
    elseif log.to == 'syslog' then
        local res = ('syslog:identity=%s,facility=%s'):format(
            log.syslog.identity,
            log.syslog.facility)
        -- TODO: Syslog's URI format is different from tarantool's
        -- one for Unix domain sockets: `unix:/path/to/socket` vs
        -- `unix/:/path/to/socket`. We expect syslog's format
        -- here, but maybe it worth to accept our own one (or even
        -- just path) and transform under hood.
        if log.syslog.server ~= nil then
            res = res .. (',server=%s'):format(log.syslog.server)
        end
        return res
    else
        assert(false)
    end
end

-- Returns nothing or {needs_retry = true}.
local function apply(config)
    local configdata = config._configdata
    local box_cfg = configdata:filter(function(w)
        return w.schema.box_cfg ~= nil
    end, {use_default = true}):map(function(w)
        return w.schema.box_cfg, w.data
    end):tomap()

    -- Construct box_cfg.replication.
    if box_cfg.replication == nil then
        box_cfg.replication = peer_uris(configdata)
    end

    -- Construct logger destination (box_cfg.log) and log modules.
    --
    -- `log.nonblock`, `log.level`, `log.format`, 'log.modules'
    -- options are marked with the `box_cfg` annotations and so
    -- they're already added to `box_cfg`.
    local cfg_log = configdata:get('log', {use_default = true})
    box_cfg.log = log_destination(cfg_log)

    -- Construct audit logger destination and audit filter (box_cfg.audit_log
    -- and audit_filter).
    --
    -- `audit_log.nonblock` and 'audit_log.filter' options are marked with the
    -- `box_cfg` annotations and so they're already added to `box_cfg`.
    local audit_log = configdata:get('audit_log', {use_default = true})
    if audit_log ~= nil and next(audit_log) ~= nil then
        box_cfg.audit_log = log_destination(audit_log)
        if audit_log.filter ~= nil then
            assert(type(audit_log.filter) == 'table')
            box_cfg.audit_filter = table.concat(audit_log.filter, ',')
        else
            box_cfg.audit_filter = 'compatibility'
        end
    end

    -- The startup process may need a special handling and differs
    -- from the configuration reloading process.
    local is_startup = type(box.cfg) == 'function'

    local failover = configdata:get('replication.failover',
        {use_default = true})

    -- Read-only or read-write?
    if failover == 'off' then
        -- 'rw' and 'ro' mean itself.
        --
        -- The default is determined depending of amount of
        -- instances in the given replicaset.
        --
        -- * 1 instance: read-write.
        -- * >1 instances: read-only.
        local mode = configdata:get('database.mode', {use_default = true})
        if mode == 'ro' then
            box_cfg.read_only = true
        elseif mode == 'rw' then
            box_cfg.read_only = false
        elseif #configdata:peers() == 1 then
            assert(mode == nil)
            box_cfg.read_only = false
        elseif #configdata:peers() > 1 then
            assert(mode == nil)
            box_cfg.read_only = true
        else
            assert(false)
        end
    elseif failover == 'manual' then
        -- Set RO/RW based on the 'leader' replicaset option.
        --
        -- NB: If there is no configured leader, all the instances
        -- of the given replicaset are configured as read-only.
        box_cfg.read_only = not configdata:is_leader()
    elseif failover == 'election' then
        -- Enable leader election.
        if box_cfg.election_mode == nil then
            box_cfg.election_mode = 'candidate'
        end

        -- A particular instance may participate in the replicaset
        -- in different roles.
        --
        -- The default role is 'candidate' -- it votes and may be
        -- elected as a leader. The RO/RW mode is determined by
        -- the election results. box.cfg({read_only = false})
        -- means that we don't impose any extra restrictions.
        --
        -- 'voter' only votes for a new leader, but can't be
        -- elected. The actual database mode is RO (it is
        -- controlled by the underneath logic, so we pass
        -- read_only = false here).
        --
        -- An instance in the 'off' election mode neither votes,
        -- nor can be elected. It just fetches all the data.
        -- This is the only election mode, where the read-only
        -- database mode is forced by the configuration applying
        -- code.
        --
        -- An instance in the 'manual' election mode acts as a
        -- voter until box.ctl.promote() is called on it. After
        -- that it starts a new election round and acts as a
        -- candidate during this (and only this) round. No extra
        -- RO/RW restriction is performed there (controlled by the
        -- underneath logic).
        if box_cfg.election_mode == 'off' then
            box_cfg.read_only = true  -- forced RO
        elseif box_cfg.election_mode == 'voter' then
            box_cfg.read_only = false -- means no restrictions
        elseif box_cfg.election_mode == 'manual' then
            box_cfg.read_only = false -- means no restrictions
        elseif box_cfg.election_mode == 'candidate' then
            box_cfg.read_only = false -- means no restrictions
        else
            assert(false)
        end
    elseif failover == 'supervised' then
        -- The startup flow in the 'supervised' failover mode is
        -- the following.
        --
        -- * Look over the peer names of the replicaset and choose
        --   the minimal name (compare them lexicographically).
        -- * The instance with the minimal name starts in the RW
        --   mode to be able to bootstrap the replicaset if there
        --   is no local snapshot. Otherwise, it starts as usual,
        --   in RO.
        -- * All the other instances are started in RO.
        -- * The instance that is started in RW re-checks, whether
        --   it was a bootstrap leader (box.info.id == 1) and, if
        --   not, goes to RO.
        --
        -- The algorithm leans on the replicaset bootstrap process
        -- that chooses an RW instance as the bootstrap leader.
        -- bootstrap_strategy 'auto' fits this criteria.
        --
        -- As result, all the instances are in RO or one is in RW.
        -- The supervisor (failover agent) manages the RO/RW mode
        -- during the lifetime of the replicaset.
        --
        -- The 'minimal name' criteria is chosen with idea that
        -- instances are often names like 'storage-001',
        -- 'storage-002' and so on, so a probability that a newly
        -- added instance has the minimal name is low.
        --
        -- The RO/RW mode remains unchanged on reload.
        local am_i_bootstrap_leader = false
        if is_startup then
            local instance_name = configdata:names().instance_name
            am_i_bootstrap_leader =
                snapshot.get_path(configdata._iconfig_def) == nil and
                instance_name == configdata:bootstrap_leader_name()
            box_cfg.read_only = not am_i_bootstrap_leader
        end

        -- It is possible that an instance with the minimal
        -- name (configdata:bootstrap_leader_name()) is added to
        -- the configuration, when the replicaset is already
        -- bootstrapped.
        --
        -- Ideally, we shouldn't make this instance RW. However,
        -- we can't differentiate this situation from one, when
        -- the replicaset bootstrap is needed, before a first
        -- box.cfg().
        --
        -- However, after leaving box.cfg() or from a background
        -- fiber after box.ctl.wait_rw() we can check that the
        -- instance was a bootstrap leader using the
        -- box.info.id == 1 condition.
        --
        -- If box.info.id != 1, then the replicaset was already
        -- bootstrapped and so we should go to RO.
        if am_i_bootstrap_leader then
            fiber.new(function()
                local name = 'config_set_read_only_if_not_bootstrap_leader'
                fiber.self():name(name, {truncate = true})
                box.ctl.wait_rw()
                if box.info.id ~= 1 then
                    -- Not really a bootstrap leader, just a
                    -- replica. Go to RO.
                    box.cfg({read_only = true})
                end
            end)
        end
    else
        assert(false)
    end

    assert(type(box.cfg) == 'function' or type(box.cfg) == 'table')
    if type(box.cfg) == 'table' then
        local box_cfg_nondynamic = configdata:filter(function(w)
            return w.schema.box_cfg ~= nil and w.schema.box_cfg_nondynamic
        end, {use_default = true}):map(function(w)
            return w.schema.box_cfg, w.data
        end):tomap()
        box_cfg_nondynamic.log = box_cfg.log
        box_cfg_nondynamic.audit_log = box_cfg.audit_log
        box_cfg_nondynamic.audit_filter = box_cfg.audit_filter
        for k, v in pairs(box_cfg_nondynamic) do
            if v ~= box.cfg[k] then
                local warning = 'box_cfg.apply: non-dynamic option '..k..
                    ' will not be set until the instance is restarted'
                config:_alert({type = 'warn', message = warning})
                box_cfg[k] = nil
            end
        end
    end

    -- Persist an instance name to protect a user from accidental
    -- attempt to run an instance from a snapshot left by another
    -- instance.
    --
    -- Persist a replicaset name to protect a user from attempt to
    -- mix instances with data from different replicasets into one
    -- replicaset.
    local names = configdata:names()
    box_cfg.instance_name = names.instance_name
    box_cfg.replicaset_name = names.replicaset_name
    box_cfg.instance_uuid = names.instance_uuid
    box_cfg.replicaset_uuid = names.replicaset_uuid

    -- Set bootstrap_leader option.
    box_cfg.bootstrap_leader = configdata:bootstrap_leader()

    -- Set metrics option.
    local include = configdata:get('metrics.include', {use_default = true})
    local exclude = configdata:get('metrics.exclude', {use_default = true})
    local labels = configdata:get('metrics.labels', {use_default = true})

    box_cfg.metrics = {
        include = include or { 'all' },
        exclude = exclude or { },
        labels = labels or { alias = names.instance_name },
    }

    -- First box.cfg() call.
    --
    -- Force the read-only mode if:
    --
    -- * the startup may take a long time, and
    -- * the instance is not the only one in its replicaset, and
    -- * there is an existing snapshot (otherwise we wouldn't able
    --   to assign a bootstrap leader).
    --
    -- The reason is that the configured master may be switched
    -- while it is starting. In this case it is undesirable to set
    -- RW mode if the actual configuration marks the instance as
    -- RO.
    --
    -- The main startup code calls this applier second time if
    -- {needs_retry = true} is returned after re-reading of the
    -- configuration.
    --
    -- Automatic reapplying of the post-startup configuration is
    -- performed for all the cases, when the startup may take a
    -- long time.
    --
    -- This logic is disabled if the automatic leader election is
    -- in effect. The configuration has no leadership information,
    -- in this case, so there is no much sense to re-read it after
    -- startup.
    --
    -- The same for the supervised failover mode.
    if is_startup and failover ~= 'election' and failover ~= 'supervised' then
        local configured_as_rw = not box_cfg.read_only
        local in_replicaset = #configdata:peers() > 1
        local has_snap = snapshot.get_path(configdata._iconfig_def) ~= nil

        -- Require at least one writable instance in the
        -- replicaset if the instance is to be bootstrapped (has
        -- no existing snapshot). Otherwise there is no one who
        -- would register the instance in the _cluster system
        -- space.
        --
        -- TODO: If an instance is configured with known
        -- instance_uuid that is already in _cluster, it may be
        -- re-bootstrapped. But we don't know from the
        -- configuration whether the given instance_uuid is
        -- already registered. We should decide what to do in the
        -- case.
        --
        -- TODO: An anonymous replica is not to be registered in
        -- _cluster. So, it can subscribe to such a replicaset.
        -- We should decide what to do in the case.
        if in_replicaset and not has_snap then
            local has_rw = false
            if failover == 'off' then
                for _, peer_name in ipairs(configdata:peers()) do
                    local opts = {peer = peer_name, use_default = true}
                    local mode = configdata:get('database.mode', opts)
                    -- NB: The default is box.NULL that is
                    -- interpreted as 'ro' for a replicaset with
                    -- more than one instance.
                    if mode == 'rw' then
                        has_rw = true
                        break
                    end
                end
            elseif failover == 'manual' then
                has_rw = configdata:leader() ~= nil
            else
                assert(false)
            end
            if not has_rw then
                error(('Startup failure.\nNo leader to register new ' ..
                    'instance %q. All the instances in replicaset %q of ' ..
                    'group %q are configured to the read-only mode.'):format(
                    names.instance_name, names.replicaset_name,
                    names.group_name), 0)
            end
        end

        -- Reading a snapshot may take a long time, fetching it
        -- from a remote master is even longer.
        local startup_may_be_long = has_snap or
            (not has_snap and in_replicaset)

        -- Start to construct the force read-only mode condition.
        local force_read_only = configured_as_rw

        -- It only has sense to force RO if we expect that the
        -- configured RO/RW mode has a good chance to change
        -- during the startup.
        force_read_only = force_read_only and startup_may_be_long

        -- If the instance is the only one in its replicaset,
        -- there is no another one that would take the leadership.
        --
        -- It is OK to just start in RW.
        force_read_only = force_read_only and in_replicaset

        -- Can't set RO for an instance in a replicaset without an
        -- existing snapshot, because one of the cases is when the
        -- replicaset is to be created from scratch. There should
        -- be at least one RW instance to act as a bootstrap
        -- leader.
        --
        -- We don't know, whether other instances have snapshots,
        -- so the best that we can do is to lean on the user
        -- provided configuration regarding RO/RW.
        force_read_only = force_read_only and has_snap

        if force_read_only then
            box_cfg.read_only = true
        end

        log.debug('box_cfg.apply (startup): %s', box_cfg)
        box.cfg(box_cfg)

        -- NB: needs_retry should be true when force_read_only is
        -- true. It is so by construction.
        return {needs_retry = startup_may_be_long}
    end

    -- If it is reload, just apply the new configuration.
    log.debug('box_cfg.apply: %s', box_cfg)
    box.cfg(box_cfg)
end

return {
    name = 'box_cfg',
    apply = apply,
}
