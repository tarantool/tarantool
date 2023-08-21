local urilib = require('uri')
local fio = require('fio')
local log = require('internal.config.utils.log')
local instance_config = require('internal.config.instance_config')

-- Accept a comma separated list of URIs and return the first one
-- that is suitable to create a client socket (not just to listen
-- on the server as, say, 0.0.0.0:3301 or localhost:0).
--
-- See the uri_is_suitable_to_connect() method in the instance
-- schema object for details.
local function find_suitable_uri_to_connect(uris)
    for _, u in ipairs(urilib.parse_many(uris)) do
        if instance_config:uri_is_suitable_to_connect(u) then
            -- The urilib.format() call has the second optional
            -- argument `write_password`. Let's assume that the
            -- given URIs are to listen on them and so have no
            -- user/password.
            return urilib.format(u)
        end
    end
    return nil
end

local function find_password(configdata, username)
    -- The guest user can't have a password.
    if username == 'guest' then
        return nil
    end

    -- Find a user definition in the config.
    local user_def = configdata:get('credentials.users.' .. username,
        {use_default = true})
    if user_def == nil then
        error(('box_cfg.apply: cannot find user %s in the config to use its ' ..
            'password in a replication peer URI'):format(username), 0)
    end

    -- There is a user definition without a password. Let's assume
    -- that the user has no password.
    if user_def.password ~= nil then
        return user_def.password.plain
    end
    return nil
end

local function peer_uri(configdata, peer_name)
    local opts = {peer = peer_name, use_default = true}
    local listen = configdata:get('iproto.listen', opts)
    local advertise = configdata:get('iproto.advertise.peer', opts)

    if advertise ~= nil and not advertise:endswith('@') then
        -- The iproto.advertise.peer option contains an URI.
        --
        -- There are the following cases.
        --
        -- * host:port
        -- * user@host:port
        -- * user:pass@host:port
        --
        -- Note: the host:port part may represent a Unix domain
        -- socket: host = 'unix/', port = '/path/to/socket'.
        --
        -- The second case needs additional handling: we should
        -- find password for the given user in the 'credential'
        -- section of the config.
        --
        -- Otherwise, the URI is returned as is.
        local u, err = urilib.parse(advertise)
        -- NB: The URI is validated, so the parsing can't fail.
        assert(u ~= nil, err)
        if u.login ~= nil and u.password == nil then
            u.password = find_password(configdata, u.login)
            return urilib.format(u, true)
        end
        return advertise
    elseif listen ~= nil then
        -- The iproto.advertise.peer option has no URI.
        --
        -- There are the following cases.
        --
        -- * <no iproto.advertise.peer>
        -- * user@
        -- * user:pass@
        --
        -- In any case we should find an URI suitable to create a
        -- client socket in iproto.listen option. After this, add
        -- the auth information if any.
        local uri = find_suitable_uri_to_connect(listen)
        if uri == nil then
            return nil
        end

        -- No additional auth information in iproto.advertise.peer:
        -- return the listen URI as is.
        if advertise == nil then
            return uri
        end

        -- Extract user and password from the iproto.advertise
        -- option. If no password given, find it in the
        -- 'credentials' section of the config.
        assert(advertise:endswith('@'))
        local auth = advertise:sub(1, -2):split(':', 1)
        local username = auth[1]
        local password = auth[2] or find_password(configdata, username)

        -- Rebuild the listen URI with the given username and
        -- password,
        local u, err = urilib.parse(uri)
        -- NB: The URI is validated, so the parsing can't fail.
        assert(u ~= nil, err)
        u.login = username
        u.password = password
        return urilib.format(u, true)
    end

    return nil
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

local function log_destination(configdata)
    local log = configdata:get('log', {use_default = true})
    if log.to == 'stderr' then
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

-- Determine where snapshot should reside based on the given
-- configuration.
--
-- To be called before first box.cfg().
local function effective_snapshot_dir(configdata)
    -- The snapshot directory has a default value in the schema
    -- (it is a string). So, it can't be nil or box.NULL.
    local snap_dir = configdata:get('snapshot.dir', {use_default = true})
    assert(snap_dir ~= nil)

    -- If the path is absolute, just return it.
    --
    -- This check is necessary due to fio.pathjoin() peculiars,
    -- see gh-8816.
    if snap_dir:startswith('/') then
        return snap_dir
    end

    -- We assume that the startup working directory is the current
    -- working directory. IOW, that this function is called before
    -- first box.cfg() call. Let's verify it.
    assert(type(box.cfg) == 'function')

    -- If the snapshot directory is not absolute, it is relative
    -- to the working directory.
    --
    -- Determine an absolute path to the configured working
    -- directory considering that it may be relative to the
    -- working directory at the startup moment.
    local work_dir = configdata:get('process.work_dir', {use_default = true})
    if work_dir == nil then
        work_dir = '.'
    end
    work_dir = fio.abspath(work_dir)

    -- Now we know the absolute path to the configured working
    -- directory. Let's determine the snapshot directory path.
    return fio.abspath(fio.pathjoin(work_dir, snap_dir))
end

-- Determine whether the instance will be started from an existing
-- snapshot.
--
-- To be called before first box.cfg().
local function has_snapshot(configdata)
    local pattern = fio.pathjoin(effective_snapshot_dir(configdata), '*.snap')
    return #fio.glob(pattern) > 0
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
    box_cfg.log = log_destination(configdata)

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

    -- The startup process may need a special handling and differs
    -- from the configuration reloading process.
    local is_startup = type(box.cfg) == 'function'

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
    if is_startup and failover ~= 'election' then
        local configured_as_rw = not box_cfg.read_only
        local in_replicaset = #configdata:peers() > 1
        local has_snap = has_snapshot(configdata)

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
