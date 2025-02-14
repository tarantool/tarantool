local fiber = require('fiber')
local log = require('internal.config.utils.log')
local instance_config = require('internal.config.instance_config')
local snapshot = require('internal.config.utils.snapshot')
local schedule_task = fiber._internal.schedule_task
local version = require('version')
local tarantool = require('tarantool')
local clock = require('clock')

-- {{{ Collect options with the box_cfg annotation

local function collect_by_box_cfg_annotation(configdata)
    return configdata:filter(function(w)
        return w.schema.box_cfg ~= nil
    end, {use_default = true}):map(function(w)
        return w.schema.box_cfg, w.data
    end):tomap()
end

-- }}} Collect options with the box_cfg annotation

-- {{{ iproto.listen

local function set_iproto_listen(configdata, box_cfg)
    -- Explicitly set box_cfg.listen to box.NULL if iproto.listen is not
    -- provided.
    -- TODO: drop this when default for array value will be supported.
    if configdata:get('iproto.listen') == nil then
        box_cfg.listen = box.NULL
    end
end

-- }}} iproto.listen

-- {{{ replication.peers

local function peer_uris(configdata)
    local peers = configdata:peers()
    if #peers <= 1 then
        -- box.NULL means 'a default value' for box.cfg(), while
        -- nil means 'leave a previous value'. We need the
        -- default: there are no configured upstreams.
        return box.NULL
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
        local iconfig_def = configdata._peers[peer_name].iconfig_def
        -- Don't use anonymous replicas as upstreams.
        --
        -- An anonymous replica can't be an upstream for a
        -- non-anonymous instance.
        --
        -- An anonymous replica can be an upstream for another
        -- anonymous replica, but only non-anonymous peers are set
        -- as upstreams by default.
        --
        -- A user may configure a custom data flow using
        -- `replication.peers` option.
        local is_anon = instance_config:get(iconfig_def, 'replication.anon')
        -- Don't replicate data from an isolated instance.
        --
        -- While the isolated instance goes to RO and drops iproto
        -- connections, we refuse to fetch data from it from our
        -- side. It elimimates unnecessary errors in logs at
        -- least.
        local isolated = instance_config:get(iconfig_def, 'isolated')
        if not is_anon and not isolated then
            local uri = instance_config:instance_uri(iconfig_def, 'peer',
                {log_prefix = "replicaset dataflow configuration: "})
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
    end

    if not has_upstream then
        error(('%s: no suitable peer URIs found'):format(err_msg_prefix), 0)
    end

    return uris
end

local function set_replication_peers(configdata, box_cfg)
    -- Construct box_cfg.replication.
    if box_cfg.replication == nil then
        box_cfg.replication = peer_uris(configdata)
    end
end

-- }}} replication.peers

-- {{{ log/audit_log helpers

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

-- }}} log/audit_log helpers

-- {{{ log

local function set_log(configdata, box_cfg)
    -- Construct logger destination (box_cfg.log) and log modules.
    --
    -- `log.nonblock`, `log.level`, `log.format`, 'log.modules'
    -- options are marked with the `box_cfg` annotations and so
    -- they're already added to `box_cfg`.
    local cfg_log = configdata:get('log', {use_default = true})
    box_cfg.log = log_destination(cfg_log)

    -- TODO(gh-10756): This is not needed when :apply_default()
    -- supports default values for composite types.
    if type(box_cfg.log_modules) == 'nil' then
        box_cfg.log_modules = box.NULL
    end
end

-- }}} log

-- {{{ audit_log

local function set_audit_log(configdata, box_cfg)
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

    -- TODO(gh-10756): This is not needed when :apply_default()
    -- supports default values for composite types.
    if tarantool.package == 'Tarantool Enterprise' and
       type(box_cfg.audit_spaces) == 'nil' then
        box_cfg.audit_spaces = box.NULL
    end
end

-- }}} audit_log

-- {{{ wal.ext

local function set_wal_ext(_configdata, box_cfg)
    -- TODO(gh-10756): This is not needed when :apply_default()
    -- supports default values for composite types.
    if tarantool.package == 'Tarantool Enterprise' and
       type(box_cfg.wal_ext) == 'nil' then
        box_cfg.wal_ext = box.NULL
    end
end

-- }}} wal.ext

-- {{{ Set RO/RW

local function set_ro_rw(configdata, box_cfg)
    -- The startup process may need a special handling and differs
    -- from the configuration reloading process.
    local is_startup = type(box.cfg) == 'function'

    local failover = configdata:get('replication.failover',
        {use_default = true})
    local is_anon = configdata:get('replication.anon', {use_default = true})

    -- Read-only or read-write?
    if failover == 'off' then
        -- 'rw' and 'ro' mean itself.
        --
        -- The default is determined depending of amount of
        -- instances in the given replicaset.
        --
        -- * 1 instance: read-write.
        -- * >1 instances: read-only.
        --
        -- NB: configdata.lua verifies that there is at least one
        -- non-anonymous instance. So, an anonymous replica is
        -- read-only by default.
        --
        -- NB: configdata.lua also verifies that read-write mode
        -- is not enabled for an anonymous replica in the config.
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
        --
        -- NB: configdata.lua verifies that an anonymous replica
        -- is not set as a leader.
        box_cfg.read_only = not configdata:is_leader()
    elseif failover == 'election' then
        -- Enable leader election on non-anonymous instances.
        if box_cfg.election_mode == nil then
            box_cfg.election_mode = is_anon and 'off' or 'candidate'
        end

        -- An anonymous replica can be configured with
        -- `election_mode: off`, but not other modes.
        --
        -- The validation is performed in configdata.lua for all
        -- the peers of the given replicaset.
        if is_anon then
            assert(box_cfg.election_mode == 'off')
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
        --   the minimal name across all non-anonymous instances
        --   (compare them lexicographically).
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
end

-- }}} Set RO/RW

-- Modify box-level configuration values and perform other actions
-- to enable the isolated mode (if configured).
local function switch_isolated_mode_before_box_cfg(config, box_cfg)
    local configdata = config._configdata

    -- If the isolated mode is not enabled, there is nothing to do.
    if not configdata:get('isolated', {use_default = true}) then
        return
    end

    -- Issue a warning to highlight the unusual instance status to
    -- the administrator.
    local key = 'isolated_mode_enabled'
    local message = ('The isolated mode is set for the instance %q'):format(
        config._instance_name)
    config._aboard:set({
        type = 'warn',
        message = message,
    }, {
        key = key,
    })

    -- An application or a role may perform background database
    -- modification if the instance is in the RW mode: for
    -- example, a role may perform eviction of stale records.
    -- If the instance is in the isolated mode, it should be in RO
    -- to don't produce any new transactions.
    --
    -- The reason is that these transactions will be sent to other
    -- replicaset members and applied on them, when the instance
    -- goes from the isolated mode. At the same time, the
    -- non-isolated part of the replicaset may serve requests and
    -- perform data modifications. An attempt to modify the same
    -- data from two instances may break data integrity[^1].
    --
    -- It is recommended to extract all the needed data from the
    -- isolated instance and perform the modifications on the
    -- current leader (in the non-isolated part of the
    -- replicaset).
    --
    -- [^1]: Unless the data operations are carefully designed to
    --       be idempotent to use in the master-master mode.
    --
    -- TODO(gh-10404): Set ro_reason=isolated.
    box_cfg.read_only = true

    -- Don't accept new iproto connections.
    --
    -- An instance in the isolated mode shouldn't process a
    -- traffic from a user.
    box_cfg.listen = box.NULL

    -- Don't replicate data from other replicaset members.
    --
    -- The isolated mode can be enabled to stop data modifications
    -- on the given instance, including ones from the replication.
    -- It may help to debug a problem or extract some needed data.
    box_cfg.replication = box.NULL
end

-- Perform post-box-cfg actions to enable the isolated mode (if
-- configured).
local function switch_isolated_mode_after_box_cfg(config)
    local configdata = config._configdata

    -- If the isolated mode is not enabled, there is nothing to do.
    if not configdata:get('isolated', {use_default = true}) then
        return
    end

    -- Drop existing iproto connections.
    --
    -- An instance in the isolated mode shouldn't process a
    -- traffic from a user.
    --
    -- Here we assume that when box.cfg() is finished, iproto
    -- listening sockets are already closed and no new connections
    -- may be created. So, the only thing to do is to close the
    -- existing connections.
    --
    -- NB: It seems safe to start this fiber several times in
    -- parallel, so there are no checks, whether such a fiber
    -- already exists.
    fiber.new(function()
        local name = 'isolated_mode_drop_iproto_connections'
        fiber.self():name(name, {truncate = true})

        -- A note regarding box.iproto.internal.drop_connections.
        --
        -- It seems, the current behavior if connections are not
        -- closed in time is to report the timeout error and
        -- continue closing connection from the given generation
        -- in background.
        --
        -- It means that the passed timeout doesn't affect the
        -- behavior of the process of closing connections itself
        -- and can be only used to let the caller know whether
        -- and when the connections (existed at the moment of the
        -- invocation) are closed.
        --
        -- Taking it into account, the timeout in this code is
        -- not configurable and chosen more or less arbitrarily.
        -- It only affects whether and when an alert is issued.
        --
        -- If we find a need in a future, we can make it
        -- configurable.
        local timeout = 10

        -- Testing requires to redefine the timeout. Use an
        -- environment variable for that.
        local env_var_name = 'TT_CONFIG_DROP_CONNECTION_TIMEOUT'
        local redefined_timeout = tonumber(os.getenv(env_var_name))
        if redefined_timeout ~= nil then
            log.warn('isolated mode: %s redefines the iproto connections ' ..
                'drop timeout: %d', env_var_name, redefined_timeout)
            timeout = redefined_timeout
        end

        local start_time = clock.monotonic()
        local ok, err = pcall(box.iproto.internal.drop_connections, timeout)

        -- An unique key is set here to don't duplicate the alert
        -- if the box_cfg.apply function is called twice within
        -- the same startup/reconfiguration process.
        --
        -- Currently, it is possible on startup due to the safe
        -- startup process. See c80b121544f6 ("config: add safe
        -- startup mode") for details.
        --
        -- Also, this key is used to drop the alert (if any) after
        -- a successful `drop_connections` call.
        local alert_key = 'isolated_mode_drop_iproto_connections_timeout'

        if not ok then
           local message = ('isolated mode: can\'t drop iproto connections ' ..
                'during %d seconds (continued in background): %s'):format(
                timeout, err)

            -- There is no clear point, when to drop this alert.
            --
            -- Once appeared, it exists till the next
            -- reconfiguration, because all the alerts are dropped
            -- before applying the new configuration.
            --
            -- Also, dropped on a successful attempt to drop
            -- connections.
            config._aboard:set({
                type = 'warn',
                message = message,
            }, {
                key = alert_key,
            })
            return
        end

        -- If it appears that there were several reconfigurations
        -- in row, each schedules a fiber to drop the connections,
        -- but the fibers are executed after the reconfigurations,
        -- and the first issued an alert, but the next
        -- successfully dropped the connections, let's drop this
        -- alert.
        --
        -- No-op if there is no alert with the given key.
        config._aboard:drop(alert_key)

        local duration = clock.monotonic() - start_time
        log.info('isolated mode: successfully dropped iproto connections ' ..
            '(took %d seconds)', duration)
    end)
end

--------------------------------------------------------------------------------
--  Instance/replicaset names
--------------------------------------------------------------------------------

local names_state = {
    -- Used to access confidata and drop alerts.
    config = nil,
    -- Shows, whether triggers for setting names are configured.
    is_configured = false,
    -- Waiting for rw status.
    rw_watcher = nil,
    -- Shows, whether schema upgrade trigger should be deleted.
    is_upgrade_wait = false,
}

-- Make alerts for all names, which are missing (from snapshot or
-- from box.info) and which have not be alerted already.
local function names_alert_missing(config, missing_names)
    local msg = 'box_cfg.apply: name %s for %s uuid is missing from the ' ..
                'snapshot. It will be automatically set when possible.'
    local replicaset_name = config._configdata._replicaset_name
    if missing_names[replicaset_name] ~= nil and
       config._aboard:get(replicaset_name) == nil then
        local replicaset_uuid = missing_names[replicaset_name]
        local warning = msg:format(replicaset_name, replicaset_uuid)
        config._aboard:set({type = 'warn', message = warning},
            {key = replicaset_name})
    end

    local unknown_msg = 'box_cfg.apply: instance %s is unknown. Possibly ' ..
                        'instance_name is not set in database and UUID is ' ..
                        'not specified. Or instance have not joined yet.'
    for name, uuid in pairs(missing_names._peers) do
        local warning
        if uuid == 'unknown' then
            warning = unknown_msg:format(name)
        else
            warning = msg:format(name, uuid)
        end
        -- Alert may be done with 'unknown' uuid. If it was so, update alert.
        local alert = config._aboard:get(name)
        if alert == nil or alert.message ~= warning then
            config._aboard:set({type = 'warn', message = warning}, {key = name})
        end
    end
end

-- Formward declaration of function, which cleans triggers.
local names_check_and_clean

local function names_box_cfg(cfg)
    local ok, err = pcall(function()
        box.cfg(cfg)
    end)
    if not ok then
        log.warn('Failed to apply name in box.cfg: %s.', err)
    end
    return ok
end

local function names_on_name_set(name)
    names_state.config._aboard:drop(name)

    local config_rs_name = names_state.config._configdata._replicaset_name
    local config_inst_name = names_state.config._configdata._instance_name
    if name == config_rs_name then
        names_box_cfg({replicaset_name = name})
    elseif name == config_inst_name then
        names_box_cfg({instance_name = name})
    end

    names_check_and_clean()
end

local function names_schema_on_replace(old, new)
    if old == nil and new and new[1] == 'replicaset_name' then
        box.on_commit(function()
            schedule_task(names_on_name_set, new[2])
        end)
    end
end

local function missing_names_is_empty(missing_names, replicaset_name)
    return not missing_names[replicaset_name] and
           table.equals(missing_names._peers, {})
end

local function names_try_set_missing()
    local configdata = names_state.config._configdata
    local replicaset_name = configdata._replicaset_name
    local missing_names = configdata:missing_names()

    if box.info.ro or missing_names_is_empty(missing_names,
                                             replicaset_name) then
        -- Somebody have done work for us, nothing to update or we're not
        -- a rw, which is possible if the function was invoked after reload.
        return
    end

    box.begin()
    -- Set replicaset_name.
    if missing_names[replicaset_name] ~= nil then
        box.space._schema:insert{'replicaset_name', replicaset_name}
    end

    -- Set names for all instances in the replicaset.
    for name, uuid in pairs(missing_names._peers) do
        if uuid ~= 'unknown' then
            local tuple = box.space._cluster.index.uuid:get(uuid)
            -- Tuple may be nil, when instance has not joined yet. Alert
            -- will be dropped, when instance is joined, nothing to do.
            if tuple ~= nil then
                box.space._cluster:update(tuple[1], {{'=', 3, name}})
            end
        end
    end
    box.commit()
end

local function names_rw_watcher()
    return box.watch('box.status', function(_, status)
        -- It's ok, if names_try_set_missing will be triggered
        -- several times. It's NoOp after first execution.
        if status.is_ro == false then
            schedule_task(names_try_set_missing)
        end
    end)
end

local function names_schema_upgrade_on_replace(old, new)
    if old == nil or new == nil then
        return
    end

    local expected_version = version.new(2, 11, 5)
    local old_version = box.internal.version_from_tuple(old)
    local new_version = box.internal.version_from_tuple(new)
    if old_version < expected_version and new_version >= expected_version then
        -- We cannot do it inside on_replace trigger, as the version
        -- is not considered set yet and we may try to set names, when
        -- schema is not yet updated, which will fail as DDL is
        -- prohibited before schema upgrade.
        box.on_commit(function()
            names_state.rw_watcher = names_rw_watcher()
        end)
    end
end

local function names_cluster_on_replace(old, new)
    local instance_name = nil
    if new ~= nil then
        instance_name = new[3]
    elseif old ~= nil then
        instance_name = names_state.config._configdata:peer_name_by_uuid(old[2])
    end

    if instance_name ~= nil then
        box.on_commit(function()
            schedule_task(names_on_name_set, instance_name)
        end)
    end
end

local function no_missing_names_alerts(config)
    local configdata = config._configdata
    if config._aboard:get(configdata._replicaset_name) ~= nil then
        return false
    end

    local peers = configdata:peers()
    for _, name in ipairs(peers) do
        if config._aboard:get(name) ~= nil then
            return false
        end
    end

    return true
end

names_check_and_clean = function()
    if no_missing_names_alerts(names_state.config) then
        names_state.is_configured = false

        box.space._schema:on_replace(nil, names_schema_on_replace)
        box.space._cluster:on_replace(nil, names_cluster_on_replace)

        if names_state.is_upgrade_wait then
            names_state.is_upgrade_wait = false
            box.space._schema:on_replace(nil, names_schema_upgrade_on_replace)
        end

        if names_state.rw_watcher ~= nil then
            names_state.rw_watcher:unregister()
        end
    end
end

local function names_apply(config, missing_names, schema_version)
    -- names_state.config should be updated, as all triggers rely on configdata,
    -- saved in it. configdata may be different from the one, we already have.
    names_state.config = config

    -- Even if everything is configured we try to make alerts one
    -- more time, as new instances without names may be found.
    names_alert_missing(config, missing_names)
    -- Don't wait for box.status to change, we may be already rw, set names
    -- on reload, if it's possible and needed.
    if schema_version and
       schema_version >= box.internal.latest_dd_version() then
        names_try_set_missing()
    end

    if names_state.is_configured then
        -- All triggers are already configured, nothing to do, but wait.
        return
    end

    box.space._schema:on_replace(names_schema_on_replace)
    box.space._cluster:on_replace(names_cluster_on_replace)

    -- Wait for rw state and schema 2.11.5 to apply names. If schema version
    -- is nil, bootstrap is going to be done.
    if schema_version and schema_version < version.new(2, 11, 5) then
        box.space._schema:on_replace(names_schema_upgrade_on_replace)
        names_state.is_upgrade_wait = true
    else
        names_state.rw_watcher = names_rw_watcher()
    end

    names_state.is_configured = true
end

local function get_schema_version_before_cfg(config)
    local snap_path = snapshot.get_path(config._configdata._iconfig_def)
    if snap_path ~= nil then
        local tuple = snapshot.get_schema_version(snap_path)
        return box.internal.version_from_tuple(tuple)
    end
    -- Bootstrap, config not found
    return nil
end

-- See hooks_new().
local hooks_mt = {
    __index = {
        add = function(self, hook, ctx)
            local entry = {
                hook = hook,
                ctx = ctx,
            }
            table.insert(self._hooks, entry)
        end,
        run = function(self)
            for _, entry in ipairs(self._hooks) do
                entry.hook(entry.ctx)
            end
        end,
    },
}

-- A little helper to collect some hooks and run them later.
--
-- Usage:
--
--  | local hooks = hooks_new()
--  |
--  | eat_pizza()
--  | hooks:add(throw_away, {what = 'pizza box'})
--  | hooks:add(wash_hands, {person = 'me'})
--  |
--  | drink_juice()
--  | hooks:add(wash_glass, {glass = 'my glass'})
--  |
--  | finish_dinner()
--  | hooks:run()
local function hooks_new()
    return setmetatable({
        _hooks = {},
    }, hooks_mt)
end

-- Returns nothing or {needs_retry = true}.
local function apply(config)
    -- There are some actions that logically correspond to some
    -- logic written before the box.cfg() call, but technically
    -- they should be run after box.cfg(). Place them here.
    local post_box_cfg_hooks = hooks_new()

    local configdata = config._configdata

    local box_cfg = collect_by_box_cfg_annotation(configdata)
    set_iproto_listen(configdata, box_cfg)
    set_replication_peers(configdata, box_cfg)
    set_log(configdata, box_cfg)
    set_audit_log(configdata, box_cfg)
    set_wal_ext(configdata, box_cfg)
    set_ro_rw(configdata, box_cfg)

    assert(type(box.cfg) == 'function' or type(box.cfg) == 'table')
    if type(box.cfg) == 'table' then
        -- Collect non-dynamic option values based on the
        -- box_cfg_nondynamic annotation.
        local box_cfg_nondynamic = configdata:filter(function(w)
            return w.schema.box_cfg ~= nil and w.schema.box_cfg_nondynamic
        end, {use_default = true}):map(function(w)
            return w.schema.box_cfg, w.data
        end):tomap()
        -- Some of the options are transformed before assigning to
        -- the box_cfg table. Use the transformed value for the
        -- comparison below.
        box_cfg_nondynamic.log = box_cfg.log
        box_cfg_nondynamic.audit_log = box_cfg.audit_log
        box_cfg_nondynamic.audit_filter = box_cfg.audit_filter
        box_cfg_nondynamic.audit_spaces = box_cfg.audit_spaces
        for k, v in pairs(box_cfg_nondynamic) do
            if v ~= box.cfg[k] then
                local warning = 'box_cfg.apply: non-dynamic option '..k..
                    ' will not be set until the instance is restarted'
                config._aboard:set({type = 'warn', message = warning})
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
    box_cfg.instance_uuid = names.instance_uuid
    box_cfg.replicaset_uuid = names.replicaset_uuid

    local is_startup = type(box.cfg) == 'function'
    local is_anon = configdata:get('replication.anon', {use_default = true})

    -- Names are applied only if they're already in snap file.
    -- Otherwise, master must apply names after box.cfg asynchronously.
    --
    -- Note: an anonymous replica has no an entry in the _cluster
    -- system space, so it can't have a persistent instance name.
    -- It is never returned by :missing_names().
    local missing_names = configdata:missing_names()
    if not is_anon and not missing_names._peers[names.instance_name] then
        box_cfg.instance_name = names.instance_name
    end
    if not missing_names[names.replicaset_name] then
        box_cfg.replicaset_name = names.replicaset_name
    end
    if not missing_names_is_empty(missing_names, names.replicaset_name) then
        if is_startup then
            local on_schema_init
            -- schema version may be nil, if bootstrap is done.
            local version = get_schema_version_before_cfg(config)
            -- on_schema init trigger isn't deleted, as it's executed only once.
            on_schema_init = box.ctl.on_schema_init(function()
                -- missing_names cannot be gathered inside on_schema_init
                -- trigger, as box.cfg is already considered configured but
                -- box.info is still not properly initialized.
                names_apply(config, missing_names, version)
                box.ctl.on_schema_init(nil, on_schema_init)
            end)
        else
            -- Note, that we try to find new missing names on every reload.
            names_apply(config, missing_names, box.internal.dd_version())
        end
    end

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

    -- RO may be enforced by the isolated mode, so we call the
    -- function after all the other logic that may set RW.
    switch_isolated_mode_before_box_cfg(config, box_cfg)
    post_box_cfg_hooks:add(switch_isolated_mode_after_box_cfg, config)

    local failover = configdata:get('replication.failover',
        {use_default = true})

    -- First box.cfg() call.
    --
    -- Force the read-only mode if:
    --
    -- * the startup may take a long time, and
    -- * the instance is not the only one in its replicaset, and
    -- * there is an existing snapshot (otherwise we wouldn't able
    --   to assign a bootstrap leader).
    --
    -- NB: The read-only mode may be enforced due to other reasons
    -- (such as enabled isolated mode) that are not specific to
    -- the startup flow.
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
        -- Note: an anonymous replica is not to be registered in
        -- _cluster. So, it can subscribe to such a replicaset.
        if in_replicaset and not has_snap and not is_anon then
            local has_rw = false
            if failover == 'off' then
                for _, peer_name in ipairs(configdata:peers()) do
                    local opts = {instance = peer_name, use_default = true}
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

        log.debug('box_cfg.apply (startup)')
        box.cfg(box_cfg)
        post_box_cfg_hooks:run()

        -- NB: needs_retry should be true when force_read_only is
        -- true. It is so by construction.
        return {needs_retry = startup_may_be_long}
    end

    -- If it is reload, just apply the new configuration.
    log.debug('box_cfg.apply')
    box.cfg(box_cfg)
    post_box_cfg_hooks:run()
end

return {
    name = 'box_cfg',
    apply = apply,
}
