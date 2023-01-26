-- load_cfg.lua - internal file

local log = require('log')
local json = require('json')
local private = box.internal
local urilib = require('uri')
local math = require('math')
local fiber = require('fiber')
local fio = require('fio')
local compat = require('tarantool').compat

local function nop() end

-- Function decorator that is used to prevent box.cfg() from
-- being called concurrently by different fibers.
local lock = fiber.channel(1)
local function locked(f)
    return function(...)
        lock:put(true)
        local status, err = pcall(f, ...)
        lock:get()
        if not status then
            error(err)
        end
    end
end

local function is_locked()
    return lock:is_full()
end

--
-- When a feature is disabled, every single mentioning of it should
-- be eliminated. Even box.cfg{} should not accept any parameters
-- related to the feature as valid. This is why they are set to nil
-- when the feature is disabled.
--

-- Feedback daemon.
local function ifdef_feedback(value)
    if private.feedback_daemon ~= nil then
        return value
    end
end

local ifdef_feedback_set_params =
    private.feedback_daemon ~= nil and
    private.feedback_daemon.set_feedback_params or nil

-- Audit log.
local has_audit = pcall(require, 'audit')

local function ifdef_audit(value)
    if has_audit then
        return value
    end
end

-- Flight recorder.
local has_flightrec, flightrec = pcall(require, 'flightrec')

local function ifdef_flightrec(value)
    if has_flightrec then
        return value
    end
end

local ifdef_flightrec_set_params = has_flightrec and flightrec.cfg or nil

-- WAL extensions.
local function ifdef_wal_ext(value)
    if private.cfg_set_wal_ext ~= nil then
        return value
    end
end

-- Security enhancements.
local function ifdef_security(value)
    if private.cfg_set_security ~= nil then
        return value
    end
end

-- all available options
local default_cfg = {
    listen              = nil,
    memtx_memory        = 256 * 1024 *1024,
    strip_core          = true,
    memtx_min_tuple_size = 16,
    memtx_max_tuple_size = 1024 * 1024,
    slab_alloc_granularity = 8,
    slab_alloc_factor   = 1.05,
    iproto_threads      = 1,
    memtx_allocator     = "small",
    work_dir            = nil,
    memtx_dir           = ".",
    wal_dir             = ".",

    vinyl_dir           = '.',
    vinyl_memory        = 128 * 1024 * 1024,
    vinyl_cache         = 128 * 1024 * 1024,
    vinyl_max_tuple_size = 1024 * 1024,
    vinyl_read_threads  = 1,
    vinyl_write_threads = 4,
    vinyl_timeout       = 60,
    vinyl_defer_deletes = false,
    vinyl_run_count_per_level = 2,
    vinyl_run_size_ratio      = 3.5,
    vinyl_range_size          = nil, -- set automatically
    vinyl_page_size           = 8 * 1024,
    vinyl_bloom_fpr           = 0.05,

    log                 = log.cfg.log,
    log_nonblock        = log.cfg.nonblock,
    log_level           = log.cfg.level,
    log_modules         = log.cfg.modules,
    log_format          = log.cfg.format,

    audit_log           = ifdef_audit(nil),
    audit_nonblock      = ifdef_audit(true),
    audit_format        = ifdef_audit('json'),
    audit_filter        = ifdef_audit('compatibility'),

    auth_type           = 'chap-sha1',
    auth_delay          = ifdef_security(0),
    disable_guest       = ifdef_security(false),
    password_lifetime_days = ifdef_security(0),
    password_min_length = ifdef_security(0),
    password_enforce_uppercase = ifdef_security(false),
    password_enforce_lowercase = ifdef_security(false),
    password_enforce_digits = ifdef_security(false),
    password_enforce_specialchars = ifdef_security(false),
    password_history_length = ifdef_security(0),

    flightrec_enabled = ifdef_flightrec(false),
    flightrec_logs_size = ifdef_flightrec(10485760),
    flightrec_logs_max_msg_size = ifdef_flightrec(4096),
    flightrec_logs_log_level = ifdef_flightrec(6),
    flightrec_metrics_interval = ifdef_flightrec(1.0),
    flightrec_metrics_period = ifdef_flightrec(60 * 3),
    flightrec_requests_size = ifdef_flightrec(10485760),
    flightrec_requests_max_req_size = ifdef_flightrec(16384),
    flightrec_requests_max_res_size = ifdef_flightrec(16384),

    io_collect_interval = nil,
    readahead           = 16320,
    snap_io_rate_limit  = nil, -- no limit
    too_long_threshold  = 0.5,
    wal_mode            = "write",
    wal_max_size        = 256 * 1024 * 1024,
    wal_dir_rescan_delay= 2,
    wal_queue_max_size  = 16 * 1024 * 1024,
    wal_cleanup_delay   = 4 * 3600,
    wal_ext             = ifdef_wal_ext(nil),
    force_recovery      = false,
    replication         = nil,
    instance_uuid       = nil,
    replicaset_uuid     = nil,
    custom_proc_title   = nil,
    pid_file            = nil,
    background          = false,
    username            = nil,
    coredump            = false,
    read_only           = false,
    hot_standby         = false,
    memtx_use_mvcc_engine = false,
    checkpoint_interval = 3600,
    checkpoint_wal_threshold = 1e18,
    checkpoint_count    = 2,
    worker_pool_threads = 4,
    election_mode       = 'off',
    election_timeout    = 5,
    election_fencing_mode = 'soft',
    replication_timeout = 1,
    replication_sync_lag = 10,
    replication_sync_timeout = 300,
    replication_synchro_quorum = "N / 2 + 1",
    replication_synchro_timeout = 5,
    replication_connect_timeout = 30,
    replication_connect_quorum = nil, -- connect all
    replication_skip_conflict = false,
    replication_anon      = false,
    replication_threads   = 1,
    bootstrap_strategy    = "auto",
    feedback_enabled      = ifdef_feedback(true),
    feedback_crashinfo    = ifdef_feedback(true),
    feedback_host         = ifdef_feedback("https://feedback.tarantool.io"),
    feedback_interval     = ifdef_feedback(3600),
    net_msg_max           = 768,
    sql_cache_size        = 5 * 1024 * 1024,
    txn_timeout           = 365 * 100 * 86400,
    txn_isolation         = "best-effort",
}

-- We need to track cfg changes done through API of distinct modules (log.cfg of
-- log module for example). We cannot use just box.cfg because it is not
-- available before box.cfg() call and other modules can be configured before
-- this moment.
local pre_load_cfg = table.copy(default_cfg)

-- On first box.cfg{} we need to know options that were already configured
-- in standalone modules (like log module). We should not apply env vars
-- for these options. pre_load_cfg is not suitable for this purpose because
-- of nil values.
local pre_load_cfg_is_set = {}


-- Whether box is loaded.
--
-- `false` when box is not configured or when the initialization
-- is in progress.
--
-- `true` when box is configured.
--
-- Use locked() wrapper to obtain reliable results.
local box_is_configured = false

local replication_sync_timeout_brief = [[
Sets the default value for box.cfg.replication_sync_timeout.
Old is 300 seconds, new is 0 seconds. New behaviour makes
box.cfg{replication = ...} call exit without waiting for
synchronisation with all the remote nodes. This means that the node
might be in 'orphan' state for some time after the box.cfg{} call
returns. Set before first box.cfg{} call in order for the option to take effect.

https://github.com/tarantool/tarantool/wiki/compat%3Abox_cfg_replication_sync_timeout
]]

-- A list of box.cfg options whose defaults are managed by compat.
local compat_options = {
    {
        name = 'replication_sync_timeout',
        brief = replication_sync_timeout_brief,
        oldval = 300,
        newval = 0,
        obsolete = nil,
        default = 'old',
    },
}

for _, option in ipairs(compat_options) do
    local option_name = 'box_cfg_' .. option.name
    compat.add_option({
        name = option_name,
        default = option.default,
        obsolete = option.obsolete,
        brief = option.brief,
        action = function(is_new)
            if is_locked() or box_is_configured then
                error("The compat  option '" .. option_name .. "' takes " ..
                      "effect only before the initial box.cfg() call")
            end
            local val = is_new and option.newval or option.oldval
            default_cfg[option.name] = val
            pre_load_cfg[option.name] = val
        end,
        run_action_now = true,
    })
end

-- types of available options
-- could be comma separated lua types or 'any' if any type is allowed
--
-- get_option_from_env() leans on the set of types in use: don't
-- forget to update it when add a new type or a combination of
-- types here.
local template_cfg = {
    listen              = 'string, number, table',
    memtx_memory        = 'number',
    strip_core          = 'boolean',
    memtx_min_tuple_size  = 'number',
    memtx_max_tuple_size  = 'number',
    slab_alloc_granularity = 'number',
    slab_alloc_factor   = 'number',
    iproto_threads      = 'number',
    memtx_allocator     = 'string',
    work_dir            = 'string',
    memtx_dir            = 'string',
    wal_dir             = 'string',
    vinyl_dir           = 'string',
    vinyl_memory        = 'number',
    vinyl_cache               = 'number',
    vinyl_max_tuple_size      = 'number',
    vinyl_read_threads        = 'number',
    vinyl_write_threads       = 'number',
    vinyl_timeout             = 'number',
    vinyl_defer_deletes       = 'boolean',
    vinyl_run_count_per_level = 'number',
    vinyl_run_size_ratio      = 'number',
    vinyl_range_size          = 'number',
    vinyl_page_size           = 'number',
    vinyl_bloom_fpr           = 'number',

    log                 = 'string',
    log_nonblock        = 'boolean',
    log_level           = 'number, string',
    log_modules         = 'table',
    log_format          = 'string',

    audit_log           = ifdef_audit('string'),
    audit_nonblock      = ifdef_audit('boolean'),
    audit_format        = ifdef_audit('string'),
    audit_filter        = ifdef_audit('string'),

    auth_type           = 'string',
    auth_delay          = ifdef_security('number'),
    disable_guest       = ifdef_security('boolean'),
    password_lifetime_days = ifdef_security('number'),
    password_min_length = ifdef_security('number'),
    password_enforce_uppercase = ifdef_security('boolean'),
    password_enforce_lowercase = ifdef_security('boolean'),
    password_enforce_digits = ifdef_security('boolean'),
    password_enforce_specialchars = ifdef_security('boolean'),
    password_history_length = ifdef_security('number'),

    flightrec_enabled = ifdef_flightrec('boolean'),
    flightrec_logs_size = ifdef_flightrec('number'),
    flightrec_logs_max_msg_size = ifdef_flightrec('number'),
    flightrec_logs_log_level = ifdef_flightrec('number'),
    flightrec_metrics_interval = ifdef_flightrec('number'),
    flightrec_metrics_period = ifdef_flightrec('number'),
    flightrec_requests_size = ifdef_flightrec('number'),
    flightrec_requests_max_req_size = ifdef_flightrec('number'),
    flightrec_requests_max_res_size = ifdef_flightrec('number'),

    io_collect_interval = 'number',
    readahead           = 'number',
    snap_io_rate_limit  = 'number',
    too_long_threshold  = 'number',
    wal_mode            = 'string',
    wal_max_size        = 'number',
    wal_dir_rescan_delay= 'number',
    wal_cleanup_delay   = 'number',
    wal_ext             = ifdef_wal_ext('table'),
    force_recovery      = 'boolean',
    replication         = 'string, number, table',
    instance_uuid       = 'string',
    replicaset_uuid     = 'string',
    custom_proc_title   = 'string',
    pid_file            = 'string',
    background          = 'boolean',
    username            = 'string',
    coredump            = 'boolean',
    checkpoint_interval = 'number',
    checkpoint_wal_threshold = 'number',
    wal_queue_max_size  = 'number',
    checkpoint_count    = 'number',
    read_only           = 'boolean',
    hot_standby         = 'boolean',
    memtx_use_mvcc_engine = 'boolean',
    txn_isolation = 'string, number',
    worker_pool_threads = 'number',
    election_mode       = 'string',
    election_timeout    = 'number',
    election_fencing_mode = 'string',
    replication_timeout = 'number',
    replication_sync_lag = 'number',
    replication_sync_timeout = 'number',
    replication_synchro_quorum = 'string, number',
    replication_synchro_timeout = 'number',
    replication_connect_timeout = 'number',
    replication_connect_quorum = 'number',
    replication_skip_conflict = 'boolean',
    replication_anon      = 'boolean',
    replication_threads   = 'number',
    bootstrap_strategy    = 'string',
    feedback_enabled      = ifdef_feedback('boolean'),
    feedback_crashinfo    = ifdef_feedback('boolean'),
    feedback_host         = ifdef_feedback('string'),
    feedback_interval     = ifdef_feedback('number'),
    net_msg_max           = 'number',
    sql_cache_size        = 'number',
    txn_timeout           = 'number',
}

local function normalize_uri_list_for_replication(port_list)
    if type(port_list) == 'table' then
        return port_list
    end
    return {port_list}
end

-- options that require special handling
local modify_cfg = {
    replication        = normalize_uri_list_for_replication,
}

local function purge_password_from_uri(uri)
    local parsed = urilib.parse(uri)
    if parsed ~= nil and parsed.password ~= nil then
        return urilib.format(parsed, false)
    end
    return uri
end

local function purge_password_from_uris(uri)
    if uri == nil then
        return nil
    end
    if type(uri) == 'table' then
        local new_table = {}
        for k, v in pairs(uri) do
            new_table[k] = purge_password_from_uri(v)
        end
        return new_table
    end
    return purge_password_from_uri(uri)
end

-- options that require modification for logging
local log_cfg_option = {
    replication = purge_password_from_uris,
}


local function check_instance_uuid()
    if box.cfg.instance_uuid ~= box.info.uuid then
        box.error(box.error.RELOAD_CFG, 'instance_uuid')
    end
end

local function check_replicaset_uuid()
    if box.cfg.replicaset_uuid ~= box.info.cluster.uuid then
        box.error(box.error.RELOAD_CFG, 'replicaset_uuid')
    end
end

-- dynamically settable options
--
-- Note: An option should be in <dynamic_cfg_skip_at_load> table
-- or should be verified in box_check_config(). Otherwise
-- load_cfg() may report an error, but box will be configured in
-- fact.
local dynamic_cfg = {
    replication             = private.cfg_set_replication,
    io_collect_interval     = private.cfg_set_io_collect_interval,
    readahead               = private.cfg_set_readahead,
    too_long_threshold      = private.cfg_set_too_long_threshold,
    snap_io_rate_limit      = private.cfg_set_snap_io_rate_limit,
    read_only               = private.cfg_set_read_only,
    memtx_memory            = private.cfg_set_memtx_memory,
    memtx_max_tuple_size    = private.cfg_set_memtx_max_tuple_size,
    vinyl_memory            = private.cfg_set_vinyl_memory,
    vinyl_max_tuple_size    = private.cfg_set_vinyl_max_tuple_size,
    vinyl_cache             = private.cfg_set_vinyl_cache,
    vinyl_timeout           = private.cfg_set_vinyl_timeout,
    vinyl_defer_deletes     = nop,
    checkpoint_count        = private.cfg_set_checkpoint_count,
    checkpoint_interval     = private.cfg_set_checkpoint_interval,
    checkpoint_wal_threshold = private.cfg_set_checkpoint_wal_threshold,
    wal_queue_max_size      = private.cfg_set_wal_queue_max_size,
    worker_pool_threads     = private.cfg_set_worker_pool_threads,
    -- do nothing, affects new replicas, which query this value on start
    wal_dir_rescan_delay    = nop,
    wal_cleanup_delay       = private.cfg_set_wal_cleanup_delay,
    custom_proc_title       = function()
        require('title').update(box.cfg.custom_proc_title)
    end,
    force_recovery          = nop,
    election_mode           = private.cfg_set_election_mode,
    election_timeout        = private.cfg_set_election_timeout,
    election_fencing_mode = private.cfg_set_election_fencing_mode,
    replication_timeout     = private.cfg_set_replication_timeout,
    replication_connect_timeout = private.cfg_set_replication_connect_timeout,
    replication_connect_quorum = private.cfg_set_replication_connect_quorum,
    replication_sync_lag    = private.cfg_set_replication_sync_lag,
    replication_sync_timeout = private.cfg_set_replication_sync_timeout,
    replication_synchro_quorum = private.cfg_set_replication_synchro_quorum,
    replication_synchro_timeout = private.cfg_set_replication_synchro_timeout,
    replication_skip_conflict = private.cfg_set_replication_skip_conflict,
    replication_anon        = private.cfg_set_replication_anon,
    bootstrap_strategy      = private.cfg_set_bootstrap_strategy,
    instance_uuid           = check_instance_uuid,
    replicaset_uuid         = check_replicaset_uuid,
    net_msg_max             = private.cfg_set_net_msg_max,
    sql_cache_size          = private.cfg_set_sql_cache_size,
    txn_timeout             = private.cfg_set_txn_timeout,
    txn_isolation           = private.cfg_set_txn_isolation,
    auth_type               = private.cfg_set_auth_type,
    auth_delay              = private.cfg_set_security,
    disable_guest           = private.cfg_set_security,
    password_lifetime_days  = private.cfg_set_security,
    password_min_length     = ifdef_security(nop),
    password_enforce_uppercase = ifdef_security(nop),
    password_enforce_lowercase = ifdef_security(nop),
    password_enforce_digits = ifdef_security(nop),
    password_enforce_specialchars = ifdef_security(nop),
    password_history_length = ifdef_security(nop),
    wal_ext                 = private.cfg_set_wal_ext
}

-- The modules that can apply all new options with single call. The
-- application should be atomic, that is if it fails the module should
-- work as before. If `cfg` is not atomic then `revert_cfg` and
-- `revert_fallback` should be set.
--
-- `revert_cfg` is used to revert config to the state before failed
-- reconfiguration. If the reverting fails too then we try to revert
-- to "safe" value given in `revert_fallback`. "safe" in sense that
-- reverting to it should always be successful.
local dynamic_cfg_modules = {
    listen = {
        cfg = private.cfg_set_listen,
        options = {
            listen = true,
        },
        skip_at_load = true,
        revert_cfg = private.cfg_set_listen,
        revert_fallback = {
            listen = nil,
        },
    },
    feedback = {
        cfg = ifdef_feedback_set_params,
        options = {
            feedback_enabled = true,
            feedback_crashinfo = true,
            feedback_host = true,
            feedback_interval = true,
        },
    },
    flightrec = {
        cfg = ifdef_flightrec_set_params,
        options = {
            flightrec_enabled = true,
            flightrec_logs_size = true,
            flightrec_logs_max_msg_size = true,
            flightrec_logs_log_level = true,
            flightrec_metrics_interval = true,
            flightrec_metrics_period = true,
            flightrec_requests_size = true,
            flightrec_requests_max_req_size = true,
            flightrec_requests_max_res_size = true,
        },
    },
    log = {
        cfg = log.box_api.cfg,
        options = {
            log = true,
            log_level = true,
            log_modules = true,
            log_format = true,
            log_nonblock = true,
        },
        skip_at_load = true,
    }
}

--
-- For some options it is important in which order they are set.
-- For example, setting 'replication', including self, before
-- 'listen' makes no sense:
--
--     box.cfg{replication = {'localhost:3301'}, listen = 3301}
--
-- Replication won't be able to connect to a not being listened
-- port. In the table below for each option can be set a number.
-- An option is set before all other options having a bigger
-- number. Options without a number are installed after others in
-- an undefined order. The table works for reconfiguration only.
-- Order of first configuration is hardcoded in C and can't be
-- changed.
--
local dynamic_cfg_order = {
    listen                  = 100,
    -- Order of replication_* options does not matter. The only
    -- rule - apply before replication itself.
    replication_timeout     = 150,
    replication_sync_lag    = 150,
    replication_sync_timeout    = 150,
    replication_synchro_quorum  = 150,
    replication_synchro_timeout = 150,
    replication_connect_timeout = 150,
    replication_connect_quorum  = 150,
    -- Apply bootstrap_strategy before replication, but after
    -- replication_connect_quorum. The latter might influence its value.
    bootstrap_strategy      = 175,
    replication             = 200,
    -- Anon is set after `replication` as a temporary workaround
    -- for the problem, that `replication` and `replication_anon`
    -- depend on each other. If anon would be configured before
    -- `replication`, it could lead to a bug, when anon is changed
    -- from true to false together with `replication`, and it
    -- would try to deanon the old `replication` before applying
    -- the new one. This should be fixed when box.cfg is able to
    -- apply some parameters together and atomically.
    replication_anon        = 250,
    -- Cleanup delay should be ignored if replication_anon is set.
    wal_cleanup_delay       = 260,
    election_mode           = 300,
    election_timeout        = 320,
    election_fencing_mode   = 320,
}

local function sort_cfg_cb(l, r)
    l = dynamic_cfg_order[l] or math.huge
    r = dynamic_cfg_order[r] or math.huge
    return l < r
end

local dynamic_cfg_skip_at_load = {
    memtx_memory            = true,
    memtx_max_tuple_size    = true,
    vinyl_memory            = true,
    vinyl_max_tuple_size    = true,
    vinyl_cache             = true,
    vinyl_timeout           = true,
    too_long_threshold      = true,
    election_mode           = true,
    election_timeout        = true,
    election_fencing_mode   = true,
    replication             = true,
    replication_timeout     = true,
    replication_connect_timeout = true,
    replication_connect_quorum = true,
    replication_sync_lag    = true,
    replication_sync_timeout = true,
    replication_synchro_quorum = true,
    replication_synchro_timeout = true,
    replication_skip_conflict = true,
    replication_anon        = true,
    bootstrap_strategy      = true,
    wal_dir_rescan_delay    = true,
    custom_proc_title       = true,
    force_recovery          = true,
    instance_uuid           = true,
    replicaset_uuid         = true,
    net_msg_max             = true,
    readahead               = true,
    auth_type               = true,
    auth_delay              = ifdef_security(true),
    disable_guest           = ifdef_security(true),
    password_lifetime_days  = ifdef_security(true),
}

-- Options that are not part of dynamic_cfg_modules and applied individually
-- can be considered as modules with single option. Load all these options
-- into dynamic_cfg_modules.
for option, api in pairs(dynamic_cfg) do
    assert(dynamic_cfg_modules[option] == nil,
           'name clash in dynamic_cfg_modules and dynamic_cfg')
    dynamic_cfg_modules[option] = {
        cfg = api,
        options = {[option] = true},
        skip_at_load = dynamic_cfg_skip_at_load[option],
    }
end

local option2module_name = {}

for module, info in pairs(dynamic_cfg_modules) do
    for option in pairs(info.options) do
        option2module_name[option] = module
    end
end

local function convert_gb(size)
    return math.floor(size * 1024 * 1024 * 1024)
end

-- Old to new config translation tables. In case a translation is
-- not 1-to-1, then a function can be used. It takes 2 parameters:
-- value of the old option, value of the new if present. It
-- returns two values - value to replace the old option and to
-- replace the new one.
local translate_cfg = {
    snapshot_count = {'checkpoint_count'},
    snapshot_period = {'checkpoint_interval'},
    slab_alloc_arena = {'memtx_memory', function(old)
        return nil, convert_gb(old)
    end},
    slab_alloc_minimal = {'memtx_min_tuple_size'},
    slab_alloc_maximal = {'memtx_max_tuple_size'},
    snap_dir = {'memtx_dir'},
    logger = {'log'},
    logger_nonblock = {'log_nonblock'},
    panic_on_snap_error = {'force_recovery', function(old)
        return nil, not old end
    },
    panic_on_wal_error = {'force_recovery', function(old)
        return nil, not old end
    },
    replication_source = {'replication'},
    rows_per_wal = {'wal_max_size', function(old, new)
        return old, new
    end},
    election_fencing_enabled = {'election_fencing_mode', function(old, new)
        if new ~= nil then return nil, new
        elseif old == false then return nil, 'off'
        elseif old == true then return nil, 'soft'
        end
    end},
    replication_connect_quorum = {'bootstrap_strategy', function(old, new)
        if new ~= nil then
            return old, new
        elseif old ~= nil then
            return old, 'legacy'
        end
    end},
}

-- Upgrade old config
local function upgrade_cfg(cfg, translate_cfg)
    if cfg == nil then
        return {}
    end
    local result_cfg = {}
    for k, v in pairs(cfg) do
        local translation = translate_cfg[k]
        if translation ~= nil then
            local new_key = translation[1]
            local transform = translation[2]
            log.warn('Deprecated option %s, please use %s instead', k, new_key)
            local new_val_orig = cfg[new_key]
            local old_val, new_val
            if transform == nil then
                new_val = v
            else
                old_val, new_val = transform(v, new_val_orig)
            end
            if new_val_orig ~= nil and
               new_val_orig ~= new_val then
                box.error(box.error.CFG, k,
                          'can not override a value for a deprecated option')
            end
            result_cfg[k] = old_val
            result_cfg[new_key] = new_val
        else
            result_cfg[k] = v
        end
    end
    return result_cfg
end

local function check_cfg_option_type(template, name, value)
    if template == 'any' then
        return
    elseif (string.find(template, ',') == nil) then
        if type(value) ~= template then
            box.error(box.error.CFG, name, "should be of type " ..
                      template)
        end
    else
        local prepared_tmpl = ',' .. string.gsub(template, ' ', '') .. ','
        local prepared_type = ',' .. type(value) .. ','
        if string.find(prepared_tmpl, prepared_type) == nil then
            box.error(box.error.CFG, name, "should be one of types " ..
                      template)
        end
    end
end

local function prepare_cfg(cfg, default_cfg, template_cfg, modify_cfg)
    if cfg == nil then
        return {}
    end
    if type(cfg) ~= 'table' then
        error("Error: cfg should be a table")
    end
    -- just pass {.. dont_check = true, ..} to disable check below
    if cfg.dont_check then
        return
    end
    local new_cfg = {}
    for k, v in pairs(cfg) do
        if template_cfg[k] == nil then
            box.error(box.error.CFG, k , "unexpected option")
        elseif v == "" or v == nil then
            -- "" and NULL = ffi.cast('void *', 0) set option to default value
            v = default_cfg[k]
        else
            check_cfg_option_type(template_cfg[k], k, v)
        end
        if modify_cfg ~= nil and type(modify_cfg[k]) == 'function' then
            v = modify_cfg[k](v)
        end
        new_cfg[k] = v
    end
    return new_cfg
end

-- Transfer options from env_cfg to cfg.
-- If skip_cfg is given then skip transferring options from this set.
local function apply_env_cfg(cfg, env_cfg, skip_cfg)
    -- Add options passed through environment variables.
    -- Here we only add options without overloading the ones set
    -- by the user.
    for k, v in pairs(env_cfg) do
        if cfg[k] == nil and (skip_cfg == nil or skip_cfg[k] == nil) then
            cfg[k] = v
        end
    end
end

local function merge_cfg(cfg, cur_cfg)
    for k,v in pairs(cur_cfg) do
        if cfg[k] == nil then
            cfg[k] = v
        elseif type(v) == 'table' then
            merge_cfg(cfg[k], v)
        end
    end
end

-- Return true if two configurations are equivalent.
local function compare_cfg(cfg1, cfg2)
    if type(cfg1) ~= type(cfg2) then
        return false
    end
    if type(cfg1) ~= 'table' then
        return cfg1 == cfg2
    end
    return table.equals(cfg1, cfg2)
end

local function rollback_module(module, oldcfg, keys, values)
    for key in pairs(keys) do
        rawset(oldcfg, key, values[key])
    end
    if module.revert_cfg == nil then
        return
    end
    local save_err = box.error.last()
    local result, err = pcall(module.revert_cfg)
    if not result then
        for key in pairs(keys) do
            log.error("failed to revert '%s' configuration option: %s",
                      key, err)
        end

        for key in pairs(module.options) do
            rawset(oldcfg, key, module.revert_fallback[key])
        end
        -- Setting to this special value should not fail
        assert(pcall(module.revert_cfg))
    end
    box.error.set(save_err)
end

local function log_changed_options(oldcfg, keys, log_basecfg)
    for key in pairs(keys) do
        local val = oldcfg[key]
        if log_basecfg == nil or
           not compare_cfg(val, log_basecfg[key]) then
            if log_cfg_option[key] ~= nil then
                val = log_cfg_option[key](val)
            end
            log.info("set '%s' configuration option to %s",
                     key, json.encode(val))
        end
    end
end

-- Call dynamic config API for all config modules/options.
--
-- @oldcfg is current global config used by config API
-- @newcfg if it is not nil then first apply it to @oldcfg
-- @log_basecfg if it is not nil then only log option that differ from
--      this cfg
--
local function reconfig_modules(module_keys, oldcfg, newcfg, log_basecfg)
    local ordered = {}
    for name in pairs(module_keys) do
        table.insert(ordered, name)
    end
    table.sort(ordered, sort_cfg_cb)

    for _, name in pairs(ordered) do
        local oldvals
        local keys = module_keys[name]
        if newcfg ~= nil then
            oldvals = {}
            for key in pairs(keys) do
                oldvals[key] = oldcfg[key]
                rawset(oldcfg, key, newcfg[key])
            end
        end
        local module = dynamic_cfg_modules[name]
        local result, err = pcall(module.cfg)
        if not result then
            if oldvals ~= nil then
                rollback_module(module, oldcfg, keys, oldvals)
            end
            error(err)
        end

        log_changed_options(oldcfg, keys, log_basecfg)
    end
end

local function reload_cfg(oldcfg, cfg)
    cfg = upgrade_cfg(cfg, translate_cfg)
    local newcfg = prepare_cfg(cfg, default_cfg, template_cfg, modify_cfg)

    local module_keys = {}
    -- iterate over original table because prepare_cfg() may store NILs
    for key in pairs(cfg) do
        if not compare_cfg(oldcfg[key], newcfg[key]) then
            local name = option2module_name[key]
            if name == nil then
                box.error(box.error.RELOAD_CFG, key);
            end
            if module_keys[name] == nil then
                module_keys[name] = {}
            end
            module_keys[name][key] = true
        end
    end

    reconfig_modules(module_keys, oldcfg, newcfg, nil)

    if type(box.on_reload_configuration) == 'function' then
        box.on_reload_configuration()
    end
end

local function load_cfg_apply_dynamic(oldcfg)
    local module_keys = {}
    for name, module in pairs(dynamic_cfg_modules) do
        if module.skip_at_load then
            log_changed_options(oldcfg, module.options, default_cfg)
        else
            for key in pairs(module.options) do
                if oldcfg[key] ~= nil then
                    module_keys[name] = module.options
                    break
                end
            end
        end
    end

    reconfig_modules(module_keys, oldcfg, nil, default_cfg)
end

local box_cfg_guard_whitelist = {
    error = true;
    internal = true;
    index = true;
    lib = true;
    session = true;
    tuple = true;
    runtime = true;
    ctl = true;
    watch = true;
    broadcast = true;
    txn_isolation_level = true;
    NULL = true;
    info = true;
    iproto = true;
};

-- List of box members that requires full box loading.
local box_restore_after_full_load_list = {
    execute = true,
}

local box = require('box')
-- Move all box members except the whitelisted to box_configured
local box_configured = {}
for k, v in pairs(box) do
    box_configured[k] = v
    if not box_cfg_guard_whitelist[k] then
        box[k] = nil
    end
end

setmetatable(box, {
    __index = function(table, index) -- luacheck: no unused args
        error(debug.traceback("Please call box.cfg{} first"))
        error("Please call box.cfg{} first")
     end
})

local function load_cfg(cfg)
    -- A user may save box.cfg (this function) before box loading
    -- and call it afterwards. We should reconfigure box in the
    -- case.
    if box_is_configured then
        reload_cfg(box.cfg, cfg)
        return
    end

    cfg = upgrade_cfg(cfg, translate_cfg)

    -- Set options passed through environment variables.
    apply_env_cfg(cfg, box.internal.cfg.env, pre_load_cfg_is_set)

    cfg = prepare_cfg(cfg, default_cfg, template_cfg, modify_cfg)
    merge_cfg(cfg, pre_load_cfg);

    -- Save new box.cfg
    box.cfg = cfg
    local status, err = pcall(private.cfg_check)
    if not status then
        box.cfg = locked(load_cfg) -- restore original box.cfg
        return error(err)
    end

    -- NB: After this point the function should not raise an
    -- error.
    --
    -- This is important to have right <box_is_configured> (this
    -- file) and <is_box_configured> (box.cc) values.
    --
    -- It also would be counter-intuitive to receive an error from
    -- box.cfg({<...>}), but find that box is actually configured.

    -- Restore box members after initial configuration.
    for k, v in pairs(box_configured) do
        if not box_restore_after_full_load_list[k] then
            box[k] = v
        end
    end
    setmetatable(box, nil)
    box.cfg = setmetatable(cfg,
        {
            __newindex = function(table, index) -- luacheck: no unused args
                error('Attempt to modify a read-only table')
            end,
            __call = locked(reload_cfg),
        })

    -- Check schema version of the snapshot we're about to recover, if any.
    -- Some schema versions (below 1.7.5) are incompatible with Tarantool 2.x
    -- When recovering from such an old snapshot, special recovery triggers on
    -- system spaces are needed in order to be able to recover and upgrade
    -- the schema then.
    -- This code is executed before load_cfg, so work_dir is not yet set.
    local snap_dir = box.cfg.memtx_dir
    if not snap_dir:startswith('/') and box.cfg.work_dir ~= nil then
        snap_dir = fio.pathjoin(box.cfg.work_dir, snap_dir)
    end
    local snap_version = private.get_snapshot_version(snap_dir)
    if snap_version then
        private.set_recovery_triggers(snap_version)
    end

    -- This call either succeeds or calls panic() / exit().
    private.cfg_load()

    if snap_version then
        private.clear_recovery_triggers()
    end
    -- This block does not raise an error: all necessary checks
    -- already performed in private.cfg_check(). See <dynamic_cfg>
    -- comment.
    --
    -- FIXME we have issues here:
    -- 1. private.cfg_check does not make all nessesery checks now
    --    (it does not check invalid config for feedback_host for example).
    -- 2. Configuring options can throw errors but we don't panic here
    --    and thus end up with not complete configuration.
    load_cfg_apply_dynamic(cfg)

    -- Restore box members that requires full box loading.
    for k, v in pairs(box_configured) do
        if box_restore_after_full_load_list[k] then
            box[k] = v
        end
    end
    box_configured = nil

    box_is_configured = true

    -- Check if schema version matches Tarantool version and print
    -- warning if it's not (in case user forgot to call
    -- box.schema.upgrade()).
    local needs, schema_version_str = private.schema_needs_upgrade()
    if needs then
        local msg = string.format(
            'Your schema version is %s while Tarantool %s requires a more'..
            ' recent schema version. Please, consider using box.'..
            'schema.upgrade().', schema_version_str, box.info.version)
        log.warn(msg)
    end
end
box.cfg = locked(load_cfg)

--
-- Parse TT_* environment variable that corresponds to given
-- option.
--
local function get_option_from_env(option)
    local param_type = template_cfg[option]
    assert(type(param_type) == 'string')

    local env_var_name = 'TT_' .. option:upper()
    local raw_value = os.getenv(env_var_name)

    if raw_value == nil or raw_value == '' then
        return nil
    end

    local err_msg_fmt = 'Environment variable %s has ' ..
        'incorrect value for option "%s": should be %s'

    -- This code lean on the existing set of template_cfg
    -- types for simplicity.
    if param_type:find('table') and raw_value:find(',') then
        assert(not param_type:find('boolean'))
        local res = {}
        for i, v in ipairs(raw_value:split(',')) do
            res[i] = tonumber(v) or v
        end
        return res
    elseif param_type:find('boolean') then
        assert(param_type == 'boolean')
        if raw_value:lower() == 'false' then
            return false
        elseif raw_value:lower() == 'true' then
            return true
        end
        error(err_msg_fmt:format(env_var_name, option, '"true" or "false"'))
    elseif param_type == 'number' then
        local res = tonumber(raw_value)
        if res == nil then
            error(err_msg_fmt:format(env_var_name, option,
                'convertible to a number'))
        end
        return res
    elseif param_type:find('number') then
        assert(not param_type:find('boolean'))
        return tonumber(raw_value) or raw_value
    else
        assert(param_type == 'string')
        return raw_value
    end
end

-- Get options from env vars for given set.
local function env_cfg(options)
    local cfg = {}
    for option in pairs(options) do
        cfg[option] = get_option_from_env(option)
    end
    return cfg
end

-- Used to propagate cfg changes done thru API of distinct modules (
-- log.cfg of log module for example).
local function update_cfg(option, value)
    if box_is_configured then
        rawset(box.cfg, option, value)
    else
        pre_load_cfg[option] = value
        pre_load_cfg_is_set[option] = true
    end
end

box.internal.prepare_cfg = prepare_cfg
box.internal.apply_env_cfg = apply_env_cfg
box.internal.merge_cfg = merge_cfg
box.internal.check_cfg_option_type = check_cfg_option_type
box.internal.update_cfg = update_cfg
box.internal.env_cfg = env_cfg

---
--- Read box configuration from environment variables.
---
box.internal.cfg = setmetatable({}, {
    __index = function(self, key)
        if key == 'env' then
            return env_cfg(template_cfg)
        end
        assert(false)
    end,
    __newindex = function(self, key, value) -- luacheck: no unused args
        error('Attempt to modify a read-only table')
    end,
})

-- gh-810:
-- hack luajit default cpath
-- commented out because we fixed luajit to build properly, see
-- https://github.com/luajit/luajit/issues/76
-- local format = require('tarantool').build.mod_format
-- package.cpath = package.cpath:gsub(
--     '?.so', '?.' .. format
-- ):gsub('loadall.so', 'loadall.' .. format)
