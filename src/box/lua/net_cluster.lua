-- This module provides connections to all replicasets of the cluster.
-- It maintains a list of connections (that actually are net.replicaset
-- connections) and allows to subscribe to an event (aka watch) on entire
-- cluster.
--
-- There two possible ways to use this module.
-- 1. Use common shared object with default options. This object is created
--  on demand. This module has `get`, `watch_leaders` etc methods that actually
--  redirected to the shared object.
-- 2. Create and use personal object via `cluster_new` module method. On this
--  way some options can be specified. Such an object will have `close`, `get`,
--  `watch_leaders` etc method.
local fiber = require('fiber')
local net_replicaset = require('internal.net.replicaset')

local cluster_cfg_defaults = {
    config_mode = 'auto',
    connect_opts = {
        reconnect_timeout = 1,
    }
}

local default_cluster

-- Helper for lazy connection creation.
local function acquire_replicaset_connection(cluster, replicaset)
    local conn = replicaset.conn
    if conn then
        return conn
    end
    conn = net_replicaset.connect(replicaset.connect_cfg or replicaset.name,
            cluster._cfg.connect_opts)
    replicaset.conn = conn
    return conn
end

-- Helper that closes connection if necessary.
local function close_replicaset_connection(replicaset)
    if replicaset.conn then
        replicaset.conn:close()
        replicaset.conn = nil
    end
end

-- Check that replicaset roles intersects with given roles.
local function replicaset_comply_roles(replicaset, roles)
    if not roles then
        -- No filter.
        return true
    end
    if not replicaset.roles then
        -- No desired roles.
        return false
    end
    for _, role in pairs(roles) do
        if replicaset.roles[role] then
            return true
        end
    end
    return false
end

-- Get closure with `key`, `value`, `instance_name` arguments that calls user
-- watcher function with signature (`key`, `value`, `replicaset_name`).
-- In addition the closure uses guard preventing concurrent execution of func
-- per each shard.
-- Note 1: Concurrent execution is only prevented for each shard. Different
--  shards can concurrently handle the event without limitations.
-- Node 2: Normally net.replicaset already prevents concurrent execution per
--  shard. But when the config is reloaded, the connections are (or can be)
--  reestablished, and specially for this case additional guard is needed.
local function wrap_watcher_function(watcher, replicaset_name)
    return function(key, value)
        local cluster = watcher._cluster
        local state = watcher._states[replicaset_name]
        if state.is_executing then
            -- Create conditional variable in lazy mode.
            if not state.wait_count then
                state.wait_count = 0
                state.wait_cond = fiber.cond()
            end
            state.wait_count = state.wait_count + 1
            repeat
                state.wait_cond:wait()
            until not state.is_executing
            state.wait_count = state.wait_count - 1
            -- Clean up conditional variable as soon as possible.
            if state.wait_count == 0 then
                state.wait_count = nil
                state.wait_cond = nil
            end
        end
        -- Check that the watcher and replicaset are still registered.
        if watcher._func and cluster._replicasets[replicaset_name] then
            state.is_executing = true
            -- Don't care about error and result, it doesn't affect anything.
            pcall(watcher._func, key, value, replicaset_name)
            state.is_executing = nil
        end
        if state.wait_cond then
            state.wait_cond:signal()
        end
    end
end

-- Subscribe watcher to one particular replicaset.
local function watch_replicaset(watcher, replicaset)
    if not replicaset_comply_roles(replicaset, watcher._opts.roles) then
        return nil
    end
    local replicaset_name = replicaset.name
    if not watcher._states[replicaset_name] then
        watcher._states[replicaset_name] = {}
    end
    local wwfunc = wrap_watcher_function(watcher, replicaset_name)
    local conn = acquire_replicaset_connection(watcher._cluster, replicaset)
    local handle = conn:watch_leader(watcher._key, wwfunc)
    watcher._states[replicaset_name].handle = handle
end

-- Load cluster config. Internal method, expects config to be correct.
local function reload_config_impl(cluster, config)
    -- Disconnect.
    for watcher in pairs(cluster._watchers) do
        for _, state in pairs(watcher._states) do
            if state.handle then
                state.handle:unregister()
                state.handle = nil
            end
        end
    end
    for _, replicaset in pairs(cluster._replicasets) do
        close_replicaset_connection(replicaset)
    end
    table.clear(cluster._replicasets)

    -- Build replicaset map.
    for replicaset_name, replicaset_cfg in pairs(config) do
        local connect_cfg = replicaset_cfg.connect_cfg
        connect_cfg = connect_cfg and table.deepcopy(connect_cfg)
        local roles = {}
        if replicaset_cfg.roles then
            for k, v in pairs(replicaset_cfg.roles) do
                if type(k) == 'number' and type(v) == 'string' then
                    roles[v] = v
                elseif type(k) == 'string' and v then
                    roles[k] = k
                end
            end
        end
        local replicaset = {
            name = replicaset_name,
            connect_cfg = connect_cfg,
            roles = roles,
        }
        cluster._replicasets[replicaset_name] = replicaset
    end

    -- Connect and subscribe if necessary.
    for _, replicaset in pairs(cluster._replicasets) do
        for watcher in pairs(cluster._watchers) do
            watch_replicaset(watcher, replicaset)
        end
    end
end

local function collect_config()
    local config = require('config')
    local replicasets = {}

    -- Find own group.
    local own_group_name
    for _, info in pairs(config:instances()) do
        if info.instance_name == box.info.name then
            own_group_name = info.group_name
            break
        end
    end

    -- Find replicasets of own group.
    for _, info in pairs(config:instances()) do
        if info.group_name ~= own_group_name then
            goto continue
        end
        local rs = replicasets[info.replicaset_name] or {roles = {}}
        for _, role in ipairs(config:get('roles',
                {instance = info.instance_name})) do
            rs.roles[role] = role
        end
        replicasets[info.replicaset_name] = rs
        ::continue::
    end

    return replicasets
end

local cluster_watcher_methods = {}

local cluster_watcher_mt = {
    __index = cluster_watcher_methods,
}

-- Check that the first argument is a valid watcher.
local function watcher_check(watcher, method)
    if getmetatable(watcher) ~= cluster_watcher_mt then
        local fmt = 'Use watcher:%s(...) instead of watcher.%s(...)'
        box.error(box.error.ILLEGAL_PARAMS, string.format(fmt, method, method))
    end
end

-- Stop watching the key.
function cluster_watcher_methods:unregister()
    watcher_check(self, 'unregister')
    for _, state in pairs(self._states) do
        if state.handle then
            state.handle:unregister()
            state.handle = nil
        end
    end
    table.clear(self._states)
    self._key = nil
    self._func = nil
    if self._cluster then
        self._cluster._watchers[self] = nil
        self._cluster = nil
    end
end

-- Converts simple value to string, quoting string values.
local function option_value_tostring(value)
    if type(value) == 'string' then
        return "'" .. value .. "'"
    elseif type(value) == 'number' or type(value) == 'boolean' then
        return tostring(value)
    else
        return '?'
    end
end

-- Check that the arguments table comply table template.
-- Template key-value pairs show possible options and their required types -
-- a string concatenated from several types with reasonable delimiter.
local function check_options(tbl, tmpl, name)
    if type(tbl) ~= 'table' then
        box.error(box.error.ILLEGAL_PARAMS, name .. ' expected to be a map')
    end
    for k, v in pairs(tmpl) do
        if not string.find(v, type(tbl[k])) and
                not string.find(v, option_value_tostring(tbl[k])) then
            local fmt = "option '%s' of %s is %s while must be %s"
            box.error(box.error.ILLEGAL_PARAMS,
                    string.format(fmt, k, name,
                            option_value_tostring(tbl[k]), v))
        end
    end
    for k in pairs(tbl) do
        if tmpl[k] == nil then
            box.error(box.error.ILLEGAL_PARAMS,
                    "unexpected option '" .. k .. "' in " .. name)
        end
    end
end

local cluster_cfg_template = {
    config_mode = "'auto' or 'manual' or nil",
    connect_opts = 'table or nil',
}

-- Helper function that checks cfg (passed to cluster_new or init) and
-- supplements it with defaults.
local function check_and_prepare_cfg(cfg)
    cfg = table.deepcopy(cfg or {})
    check_options(cfg, cluster_cfg_template, 'cluster cfg')
    for k, v in pairs(cluster_cfg_defaults) do
        if cfg[k] == nil then
            cfg[k] = v
        end
    end
    return cfg
end

local cluster_methods = {}

local cluster_mt = {
    __index = cluster_methods,
}

-- Check that the first argument is a valid cluster.
local function cluster_check(cluster, method)
    if getmetatable(cluster) ~= cluster_mt then
        local fmt = 'Use cluster:%s(...) instead of cluster.%s(...)'
        box.error(box.error.ILLEGAL_PARAMS, string.format(fmt, method, method))
    end
end

-- Create a new instance of cluster connection.
-- cfg, if present, may have two members:
--  config_mode - how the cluster connection retrieves cluster config:
--   'auto' - from standard cluster config, following changes automatically.
--   'manual' - the config must be updated using special 'reload_config' method.
--  connect_opts - options that are passed to net.replicaset.connect as the
--   second argument. Now matters only if connections are made by replicaset
--   name (using standard cluster config), otherwise connect_cfg already
--   must contain all options, see reload_congig method.
-- The cluster will have the following methods:
--  get(replicaset_name)            - see cluster_methods:get
--  get_connections(opts)           - see cluster_methods:get_connections
--  watch_leaders(key, func, opts)  - see cluster_methods:watch_leaders
--  reload_config(config)           - see cluster_methods:reload_config
--  close()                         - see cluster_methods:close
local function cluster_new(cfg)
    cfg = check_and_prepare_cfg(cfg)
    local cluster = {
        -- Map of connection state of replicasets by their names.
        _replicasets = {},
        -- List of all cluster watchers. Watcher is both key and value here.
        _watchers = {},
        -- Saved settings.
        _cfg = cfg,
    }
    setmetatable(cluster, cluster_mt)
    if cfg.config_mode == 'auto' then
        cluster._reload_watcher = box.watch('config.info', function()
            local config = collect_config()
            if not cluster._is_closed then
                reload_config_impl(cluster, config)
            end
        end)
    end
    return cluster
end

-- Connect if necessary and get net.replicaset connection by replicaset name.
function cluster_methods:get(replicaset_name)
    cluster_check(self, 'get')
    local replicaset = self._replicasets[replicaset_name]
    if replicaset then
        return acquire_replicaset_connection(self, replicaset)
    end
end

-- Get an array of net.replicaset connections (connect if necessary).
-- This method isn't that fast and shouldn't be used for high perf flows.
-- Opts may have `roles` member - a list of roles that filters result.
function cluster_methods:get_connections(opts)
    cluster_check(self, 'get_connections')
    opts = opts or {}
    local res = {}
    for replicaset_name, replicaset in pairs(self._replicasets) do
        if replicaset_comply_roles(replicaset, opts.roles) then
            res[replicaset_name] = acquire_replicaset_connection(self,
                    replicaset)
        end
    end
    return res
end

-- Watch each replicaset leader in the cluster.
-- The function is called as func(key, value, replicaset_name).
-- Opts may have `roles` member - a list of roles that narrows target
--  replicaset set.
-- Return watcher handler (now with on `unregister` method).
function cluster_methods:watch_leaders(key, func, opts)
    cluster_check(self, 'watch_leaders')
    local watcher = {
        _cluster = self,
        _key = key,
        _func = func,
        _opts = opts or {},
        _states = {},
    }
    setmetatable(watcher, cluster_watcher_mt)
    for _, replicaset in pairs(self._replicasets) do
        watch_replicaset(watcher, replicaset)
    end
    self._watchers[watcher] = watcher
    return watcher
end

-- Trigger internal load of config (in case of `config_mode` == 'auto')
-- set new cluster config (in case of `config_mode` == 'manual')
-- In the first case `config` must be nil, in the second - it must be
-- a map replicaset_name -> replicaset_cfg (table).
-- replicaset_cfg may have members:
--  connect_cfg: if provided, it will be passed to net.replicaset.connect.
--   (if not, `replicaset_name` will be passed along with connect_opts).
--  roles: optional list of roles (map or array) that the replicaset have.
--   if present, affects filtering by role in some methods.
function cluster_methods:reload_config(config)
    cluster_check(self, 'reload_config')
    if config then
        if self._cfg.config_mode ~= 'manual' then
            box.error(box.error.ILLEGAL_PARAMS,
                    'manual reload expects manual config mode')
        end
        reload_config_impl(self, config)
    else
        if self._cfg.config_mode ~= 'auto' then
            box.error(box.error.ILLEGAL_PARAMS,
                    'force reload expects auto config mode')
        end
        reload_config_impl(self, collect_config())
    end
end

-- Close all watchers and connections.
function cluster_methods:close()
    cluster_check(self, 'close')
    while true do
        local watcher = next(self._watchers)
        if not watcher then
            break
        end
        watcher:unregister()
    end
    for _, replicaset in pairs(self._replicasets) do
        close_replicaset_connection(replicaset)
    end
    table.clear(self._replicasets)
    if self._reload_watcher then
        self._reload_watcher:unregister()
        self._reload_watcher = nil
    end
    self._is_closed = true
end

-- get for default cluster (which is created if needed).
local function get(replicaset_name)
    if not default_cluster then
        default_cluster = cluster_new(cluster_cfg_defaults)
    end
    return default_cluster:get(replicaset_name)
end

-- get_connections for default cluster (which is created if needed).
local function get_connections(opts)
    if not default_cluster then
        default_cluster = cluster_new(cluster_cfg_defaults)
    end
    return default_cluster:get_connections(opts)
end

-- watch_leaders for default cluster (which is created if needed).
local function watch_leaders(key, func, opts)
    if not default_cluster then
        default_cluster = cluster_new(cluster_cfg_defaults)
    end
    return default_cluster:watch_leaders(key, func, opts)
end

-- reload_config for default cluster (which is created if needed).
local function reload_config(config)
    if not default_cluster then
        default_cluster = cluster_new(cluster_cfg_defaults)
    end
    return default_cluster:reload_config(config)
end

return {
    new = cluster_new,

    get = get,
    get_connections = get_connections,
    watch_leaders = watch_leaders,
    reload_config = reload_config,
}
