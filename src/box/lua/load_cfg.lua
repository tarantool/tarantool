-- load_cfg.lua - internal file

local ffi = require('ffi')
ffi.cdef([[
void check_cfg();
void load_cfg();
void box_set_wal_mode(const char *mode);
void box_set_listen(const char *uri);
void box_set_replication_source(const char *source);
void box_set_log_level(int level);
void box_set_readahead(int readahead);
void box_set_io_collect_interval(double interval);
void box_set_too_long_threshold(double threshold);
void box_set_snap_io_rate_limit(double limit);
void box_set_panic_on_wal_error(int);
]])

local log = require('log')

-- see default_cfg below
local default_sophia_cfg = {
    memory_limit = 0,
    threads      = 5,
    node_size    = 134217728,
    page_size    = 131072,
    compression  = "none"
}

-- all available options
local default_cfg = {
    listen              = nil,
    slab_alloc_arena    = 1.0,
    slab_alloc_minimal  = 16,
    slab_alloc_maximal  = 1024 * 1024,
    slab_alloc_factor   = 1.1,
    work_dir            = nil,
    snap_dir            = ".",
    wal_dir             = ".",
    sophia_dir          = '.',
    sophia              = default_sophia_cfg,
    logger              = nil,
    logger_nonblock     = true,
    log_level           = 5,
    io_collect_interval = nil,
    readahead           = 16320,
    snap_io_rate_limit  = nil, -- no limit
    too_long_threshold  = 0.5,
    wal_mode            = "write",
    rows_per_wal        = 500000,
    wal_dir_rescan_delay= 2,
    panic_on_snap_error = true,
    panic_on_wal_error  = true,
    replication_source  = nil,
    custom_proc_title   = nil,
    pid_file            = nil,
    background          = false,
    username            = nil ,
    coredump            = false,

    -- snapshot_daemon
    snapshot_period     = 0,        -- 0 = disabled
    snapshot_count      = 6,
}

-- see template_cfg below
local sophia_template_cfg = {
    memory_limit = 'number',
    threads      = 'number',
    node_size    = 'number',
    page_size    = 'number',
    compression  = 'string'
}

-- types of available options
-- could be comma separated lua types or 'any' if any type is allowed
local template_cfg = {
    listen              = 'string, number',
    slab_alloc_arena    = 'number',
    slab_alloc_minimal  = 'number',
    slab_alloc_maximal  = 'number',
    slab_alloc_factor   = 'number',
    work_dir            = 'string',
    snap_dir            = 'string',
    wal_dir             = 'string',
    sophia_dir          = 'string',
    sophia              = sophia_template_cfg,
    logger              = 'string',
    logger_nonblock     = 'boolean',
    log_level           = 'number',
    io_collect_interval = 'number',
    readahead           = 'number',
    snap_io_rate_limit  = 'number',
    too_long_threshold  = 'number',
    wal_mode            = 'string',
    rows_per_wal        = 'number',
    wal_dir_rescan_delay= 'number',
    panic_on_snap_error = 'boolean',
    panic_on_wal_error  = 'boolean',
    replication_source  = 'string, number',
    custom_proc_title   = 'string',
    pid_file            = 'string',
    background          = 'boolean',
    username            = 'string',
    coredump            = 'boolean',
    snapshot_period     = 'number',
    snapshot_count      = 'number',
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
    replication_source = normalize_uri,
}

-- dynamically settable options
local dynamic_cfg = {
    wal_mode                = ffi.C.box_set_wal_mode,
    listen                  = ffi.C.box_set_listen,
    replication_source      = ffi.C.box_set_replication_source,
    log_level               = ffi.C.box_set_log_level,
    io_collect_interval     = ffi.C.box_set_io_collect_interval,
    readahead               = ffi.C.box_set_readahead,
    too_long_threshold      = ffi.C.box_set_too_long_threshold,
    snap_io_rate_limit      = ffi.C.box_set_snap_io_rate_limit,
    panic_on_wal_error      = ffi.C.box_set_panic_on_wal_error,
    -- snapshot_daemon
    snapshot_period         = box.internal.snapshot_daemon.set_snapshot_period,
    snapshot_count          = box.internal.snapshot_daemon.set_snapshot_count,
    -- do nothing, affects new replicas, which query this value on start
    wal_dir_rescan_delay    = function() end
}

local dynamic_cfg_skip_at_load = {
    wal_mode                = true,
    listen                  = true,
    replication_source      = true,
    wal_dir_rescan_delay    = true,
    panic_on_wal_error      = true,
}

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
            error("Error: cfg parameter '" .. readable_name .. "' is unexpected")
        elseif v == "" or v == nil then
            -- "" and NULL = ffi.cast('void *', 0) set option to default value
            v = default_cfg[k]
        elseif template_cfg[k] == 'any' then
            -- any type is ok
        elseif type(template_cfg[k]) == 'table' then
            if type(v) ~= 'table' then
                error("Error: cfg parameter '" .. readable_name .. "' should be a table")
            end
            v = prepare_cfg(v, default_cfg[k], template_cfg[k], modify_cfg[k], readable_name)
        elseif (string.find(template_cfg[k], ',') == nil) then
            -- one type
            if type(v) ~= template_cfg[k] then
                error("Error: cfg parameter '" .. readable_name .. "' should be of type " .. template_cfg[k])
            end
        else
            local good_types = string.gsub(template_cfg[k], ' ', '');
            if (string.find(',' .. good_types .. ',', ',' .. type(v) .. ',') == nil) then
                good_types = string.gsub(good_types, ',', ', ');
                error("Error: cfg parameter '" .. readable_name .. "' should be one of types: " .. template_cfg[k])
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
    local newcfg = prepare_cfg(cfg, default_cfg, template_cfg, modify_cfg)
    -- iterate over original table because prepare_cfg() may store NILs
    for key in pairs(cfg) do
        if dynamic_cfg[key] == nil then
            box.error(box.error.RELOAD_CFG, key);
        end
    end
    for key in pairs(cfg) do
        local val = newcfg[key]
        if oldcfg[key] ~= val then
            dynamic_cfg[key](val)
            rawset(oldcfg, key, val)
            log.info("set '%s' configuration option to '%s'", key, val)
        end
    end
    if type(box.on_reload_configuration) == 'function' then
        box.on_reload_configuration()
    end
end

local box = require('box')
-- Move all box members except 'error' to box_configured
local box_configured = {}
for k, v in pairs(box) do
    box_configured[k] = v
    -- box.net.box uses box.error and box.internal
    if k ~= 'error' and k ~= 'internal' and k ~= 'index' then
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
    cfg = prepare_cfg(cfg, default_cfg, template_cfg, modify_cfg)
    apply_default_cfg(cfg, default_cfg);
    -- Save new box.cfg
    box.cfg = cfg
    if not pcall(ffi.C.check_cfg) then
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
    ffi.C.load_cfg()
    for key, fun in pairs(dynamic_cfg) do
        local val = cfg[key]
        if val ~= nil and not dynamic_cfg_skip_at_load[key] then
            fun(cfg[key])
            if val ~= default_cfg[key] then
                log.info("set '%s' configuration option to '%s'", key, val)
            end
        end
    end
end
box.cfg = load_cfg
jit.off(load_cfg)
jit.off(reload_cfg)
jit.off(box.cfg)
