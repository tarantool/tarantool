-- load_cfg.lua - internal file

local log = require('log')
local json = require('json')
local private = require('box.internal')
local urilib = require('uri')
local math = require('math')

-- all available options
local default_cfg = {
    listen              = nil,
    memtx_memory        = 256 * 1024 *1024,
    memtx_min_tuple_size = 16,
    memtx_max_tuple_size = 1024 * 1024,
    slab_alloc_factor   = 1.05,
    work_dir            = nil,
    memtx_dir           = ".",
    wal_dir             = ".",

    vinyl_dir           = '.',
    vinyl_memory        = 128 * 1024 * 1024,
    vinyl_cache         = 128 * 1024 * 1024,
    vinyl_max_tuple_size = 1024 * 1024,
    vinyl_read_threads  = 1,
    vinyl_write_threads = 2,
    vinyl_timeout       = 60,
    vinyl_run_count_per_level = 2,
    vinyl_run_size_ratio      = 3.5,
    vinyl_range_size          = 1024 * 1024 * 1024,
    vinyl_page_size           = 8 * 1024,
    vinyl_bloom_fpr           = 0.05,
    log                 = nil,
    log_nonblock        = nil,
    log_level           = 5,
    log_format          = "plain",
    io_collect_interval = nil,
    readahead           = 16320,
    snap_io_rate_limit  = nil, -- no limit
    too_long_threshold  = 0.5,
    wal_mode            = "write",
    rows_per_wal        = 500000,
    wal_max_size        = 256 * 1024 * 1024,
    wal_dir_rescan_delay= 2,
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
    checkpoint_interval = 3600,
    checkpoint_count    = 2,
    worker_pool_threads = 4,
    replication_timeout = 1,
    replication_sync_lag = 10,
    replication_connect_timeout = 4,
    replication_connect_quorum = nil, -- connect all
    replication_skip_conflict = false,
    feedback_enabled      = true,
    feedback_host         = "https://feedback.tarantool.io",
    feedback_interval     = 3600,
    net_msg_max           = 768,
}

-- types of available options
-- could be comma separated lua types or 'any' if any type is allowed
local template_cfg = {
    listen              = 'string, number',
    memtx_memory        = 'number',
    memtx_min_tuple_size  = 'number',
    memtx_max_tuple_size  = 'number',
    slab_alloc_factor   = 'number',
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
    vinyl_run_count_per_level = 'number',
    vinyl_run_size_ratio      = 'number',
    vinyl_range_size          = 'number',
    vinyl_page_size           = 'number',
    vinyl_bloom_fpr           = 'number',

    log              = 'string',
    log_nonblock     = 'boolean',
    log_level           = 'number',
    log_format          = 'string',
    io_collect_interval = 'number',
    readahead           = 'number',
    snap_io_rate_limit  = 'number',
    too_long_threshold  = 'number',
    wal_mode            = 'string',
    rows_per_wal        = 'number',
    wal_max_size        = 'number',
    wal_dir_rescan_delay= 'number',
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
    checkpoint_count    = 'number',
    read_only           = 'boolean',
    hot_standby         = 'boolean',
    worker_pool_threads = 'number',
    replication_timeout = 'number',
    replication_sync_lag = 'number',
    replication_connect_timeout = 'number',
    replication_connect_quorum = 'number',
    replication_skip_conflict = 'boolean',
    feedback_enabled      = 'boolean',
    feedback_host         = 'string',
    feedback_interval     = 'number',
    net_msg_max           = 'number',
}

local function normalize_uri(port)
    if port == nil or type(port) == 'table' then
        return port
    end
    return tostring(port);
end

-- options that require special handling
local modify_cfg = {
    listen             = normalize_uri,
    replication        = normalize_uri,
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

-- dynamically settable options
local dynamic_cfg = {
    listen                  = private.cfg_set_listen,
    replication             = private.cfg_set_replication,
    log_level               = private.cfg_set_log_level,
    log_format              = private.cfg_set_log_format,
    io_collect_interval     = private.cfg_set_io_collect_interval,
    readahead               = private.cfg_set_readahead,
    too_long_threshold      = private.cfg_set_too_long_threshold,
    snap_io_rate_limit      = private.cfg_set_snap_io_rate_limit,
    read_only               = private.cfg_set_read_only,
    memtx_max_tuple_size    = private.cfg_set_memtx_max_tuple_size,
    vinyl_max_tuple_size    = private.cfg_set_vinyl_max_tuple_size,
    vinyl_cache             = private.cfg_set_vinyl_cache,
    vinyl_timeout           = private.cfg_set_vinyl_timeout,
    checkpoint_count        = private.cfg_set_checkpoint_count,
    checkpoint_interval     = private.checkpoint_daemon.set_checkpoint_interval,
    worker_pool_threads     = private.cfg_set_worker_pool_threads,
    feedback_enabled        = private.feedback_daemon.set_feedback_params,
    feedback_host           = private.feedback_daemon.set_feedback_params,
    feedback_interval       = private.feedback_daemon.set_feedback_params,
    -- do nothing, affects new replicas, which query this value on start
    wal_dir_rescan_delay    = function() end,
    custom_proc_title       = function()
        require('title').update(box.cfg.custom_proc_title)
    end,
    force_recovery          = function() end,
    replication_timeout     = private.cfg_set_replication_timeout,
    replication_connect_quorum = private.cfg_set_replication_connect_quorum,
    replication_skip_conflict = private.cfg_set_replication_skip_conflict,
    net_msg_max             = private.cfg_set_net_msg_max,
}

local dynamic_cfg_skip_at_load = {
    wal_mode                = true,
    listen                  = true,
    replication             = true,
    replication_timeout     = true,
    replication_connect_quorum = true,
    wal_dir_rescan_delay    = true,
    custom_proc_title       = true,
    force_recovery          = true,
}

local function convert_gb(size)
    return math.floor(size * 1024 * 1024 * 1024)
end

-- Old to new config translation tables
local translate_cfg = {
    snapshot_count = {'checkpoint_count'},
    snapshot_period = {'checkpoint_interval'},
    slab_alloc_arena = {'memtx_memory', convert_gb},
    slab_alloc_minimal = {'memtx_min_tuple_size'},
    slab_alloc_maximal = {'memtx_max_tuple_size'},
    snap_dir = {'memtx_dir'},
    logger = {'log'},
    logger_nonblock = {'log_nonblock'},
    panic_on_snap_error = {'force_recovery', function (p) return not p end},
    panic_on_wal_error = {'force_recovery', function (p) return not p end},
    replication_source = {'replication'},
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
            log.warn('Deprecated option %s, please use %s instead', k, translation[1])
            local new_val
            if translation[2] == nil then
                new_val = v
            else
                new_val = translation[2](v)
            end
            if cfg[translation[1]] ~= nil and
               cfg[translation[1]] ~= new_val then
                box.error(box.error.CFG, k,
                          'can not override a value for a deprecated option')
            end
            result_cfg[translation[1]] = new_val
        else
            result_cfg[k] = v
        end
    end
    return result_cfg
end

local function prepare_cfg(cfg, default_cfg, template_cfg, modify_cfg, prefix)
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
    local readable_prefix = ''
    if prefix ~= nil and prefix ~= '' then
        readable_prefix = prefix .. '.'
    end
    local new_cfg = {}
    for k,v in pairs(cfg) do
        local readable_name = readable_prefix .. k;
        if template_cfg[k] == nil then
            box.error(box.error.CFG, readable_name , "unexpected option")
        elseif v == "" or v == nil then
            -- "" and NULL = ffi.cast('void *', 0) set option to default value
            v = default_cfg[k]
        elseif template_cfg[k] == 'any' then
            -- any type is ok
        elseif type(template_cfg[k]) == 'table' then
            if type(v) ~= 'table' then
                box.error(box.error.CFG, readable_name, "should be a table")
            end
            v = prepare_cfg(v, default_cfg[k], template_cfg[k], modify_cfg[k], readable_name)
        elseif (string.find(template_cfg[k], ',') == nil) then
            -- one type
            if type(v) ~= template_cfg[k] then
                box.error(box.error.CFG, readable_name, "should be of type "..
                    template_cfg[k])
            end
        else
            local good_types = string.gsub(template_cfg[k], ' ', '');
            if (string.find(',' .. good_types .. ',', ',' .. type(v) .. ',') == nil) then
                good_types = string.gsub(good_types, ',', ', ');
                box.error(box.error.CFG, readable_name, "should be one of types "..
                    template_cfg[k])
            end
        end
        if modify_cfg ~= nil and type(modify_cfg[k]) == 'function' then
            v = modify_cfg[k](v)
        end
        new_cfg[k] = v
    end
    return new_cfg
end

local function apply_default_cfg(cfg, default_cfg)
    for k,v in pairs(default_cfg) do
        if cfg[k] == nil then
            cfg[k] = v
        elseif type(v) == 'table' then
            apply_default_cfg(cfg[k], v)
        end
    end
end

local function reload_cfg(oldcfg, cfg)
    cfg = upgrade_cfg(cfg, translate_cfg)
    local newcfg = prepare_cfg(cfg, default_cfg, template_cfg, modify_cfg)
    -- iterate over original table because prepare_cfg() may store NILs
    for key, val in pairs(cfg) do
        if dynamic_cfg[key] == nil and oldcfg[key] ~= val then
            box.error(box.error.RELOAD_CFG, key);
        end
    end
    for key in pairs(cfg) do
        local val = newcfg[key]
        local oldval = oldcfg[key]
        if oldval ~= val then
            rawset(oldcfg, key, val)
            if not pcall(dynamic_cfg[key]) then
                rawset(oldcfg, key, oldval) -- revert the old value
                return box.error() -- re-throw
            end
            if log_cfg_option[key] ~= nil then
                val = log_cfg_option[key](val)
            end
            log.info("set '%s' configuration option to %s", key,
                json.encode(val))
        end
    end
    if type(box.on_reload_configuration) == 'function' then
        box.on_reload_configuration()
    end
end

local box_cfg_guard_whitelist = {
    error = true;
    internal = true;
    index = true;
    session = true;
    tuple = true;
    runtime = true;
    NULL = true;
};

local box = require('box')
-- Move all box members except 'error' to box_configured
local box_configured = {}
for k, v in pairs(box) do
    box_configured[k] = v
    if not box_cfg_guard_whitelist[k] then
        box[k] = nil
    end
end

setmetatable(box, {
    __index = function(table, index)
        error(debug.traceback("Please call box.cfg{} first"))
        error("Please call box.cfg{} first")
     end
})

local function load_cfg(cfg)
    box.internal.schema.init()
    cfg = upgrade_cfg(cfg, translate_cfg)
    cfg = prepare_cfg(cfg, default_cfg, template_cfg, modify_cfg)
    apply_default_cfg(cfg, default_cfg);
    -- Save new box.cfg
    box.cfg = cfg
    if not pcall(private.cfg_check)  then
        box.cfg = load_cfg -- restore original box.cfg
        return box.error() -- re-throw exception from check_cfg()
    end
    -- Restore box members after initial configuration
    for k, v in pairs(box_configured) do
        box[k] = v
    end
    setmetatable(box, nil)
    box_configured = nil
    box.cfg = setmetatable(cfg,
        {
            __newindex = function(table, index)
                error('Attempt to modify a read-only table')
            end,
            __call = reload_cfg,
        })
    private.cfg_load()
    for key, fun in pairs(dynamic_cfg) do
        local val = cfg[key]
        if val ~= nil and not dynamic_cfg_skip_at_load[key] then
            fun()
            if val ~= default_cfg[key] then
                log.info("set '%s' configuration option to %s", key, json.encode(val))
            end
        end
    end
    if not box.cfg.read_only and not box.cfg.replication then
        box.schema.upgrade{auto = true}
    end
end
box.cfg = load_cfg

-- gh-810:
-- hack luajit default cpath
-- commented out because we fixed luajit to build properly, see
-- https://github.com/luajit/luajit/issues/76
-- local format = require('tarantool').build.mod_format
-- package.cpath = package.cpath:gsub(
--     '?.so', '?.' .. format
-- ):gsub('loadall.so', 'loadall.' .. format)
