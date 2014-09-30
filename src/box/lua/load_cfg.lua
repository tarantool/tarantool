-- load_cfg.lua - internal file

local ffi = require('ffi')
ffi.cdef([[
void load_cfg();
void box_set_wal_mode(const char *mode);
void box_set_replication_source(const char *source);
void box_set_log_level(int level);
void box_set_io_collect_interval(double interval);
void box_set_too_long_threshold(double threshold);
void box_set_snap_io_rate_limit(double limit);
]])

local function normalize_uri(port)
    if port == nil then
        return nil
    end
    return tostring(port);
end

-- arguments that can be number or string
local wrapper_cfg = {
    listen             = normalize_uri,
}

-- all available options
local default_cfg = {
    listen              = nil,
    slab_alloc_arena    = 1.0,
    slab_alloc_minimal  = 64,
    slab_alloc_factor   = 2.0,
    work_dir            = nil,
    snap_dir            = ".",
    wal_dir             = ".",
    sophia_dir          = './sophia',
    logger              = nil,
    logger_nonblock     = true,
    log_level           = 5,
    io_collect_interval = nil,
    readahead           = 16320,
    snap_io_rate_limit  = nil,
    too_long_threshold  = 0.5,
    wal_mode            = "write",
    rows_per_wal        = 500000,
    wal_dir_rescan_delay= 0.1,
    panic_on_snap_error = true,
    panic_on_wal_error  = false,
    replication_source  = nil,
    custom_proc_title   = nil,
    pid_file            = nil,
    background          = false,
    username            = nil ,
    coredump            = false,

    -- snap_daemon
    snapshot_period     = 0,        -- 0 = disabled
    snapshot_count      = 6,
}

-- types of available options
-- could be comma separated lua types or 'any' if any type is allowed
local template = {
    listen              = 'string, number',
    slab_alloc_arena    = 'number',
    slab_alloc_minimal  = 'number',
    slab_alloc_factor   = 'number',
    work_dir            = 'string',
    snap_dir            = 'string',
    wal_dir             = 'string',
    sophia_dir          = 'string',
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
    replication_source  = 'string',
    custom_proc_title   = 'string',
    pid_file            = 'string',
    background          = 'boolean',
    username            = 'string',
    coredump            = 'boolean',
    snapshot_period     = 'number',
    snapshot_count      = 'number',
}

-- dynamically settable options
local dynamic_cfg = {
    wal_mode                = ffi.C.box_set_wal_mode,
    replication_source      = ffi.C.box_set_replication_source,
    log_level               = ffi.C.box_set_log_level,
    io_collect_interval     = ffi.C.box_set_io_collect_interval,
    too_long_threshold      = ffi.C.box_set_too_long_threshold,
    snap_io_rate_limit      = ffi.C.box_set_snap_io_rate_limit,

    -- snap_daemon
    snapshot_period         = box.internal.snap_daemon.set_snapshot_period,
    snapshot_count          = box.internal.snap_daemon.set_snapshot_count,
}

local function prepare_cfg(table)
    if table == nil then
        return {}
    end
    if type(table) ~= 'table' then
        error("Error: cfg should be a table")
    end
    -- just pass {.. dont_check = true, ..} to disable check below
    if table.dont_check then
        return
    end
    local newtable = {}
    for k,v in pairs(table) do
        if template[k] == nil then
            error("Error: cfg parameter '" .. k .. "' is unexpected")
        elseif v == "" or v == nil then
            -- "" and NULL = ffi.cast('void *', 0) set option to default value
            v = default_cfg[k]
        elseif template[k] == 'any' then
            -- any type is ok
        elseif (string.find(template[k], ',') == nil) then
            -- one type
            if type(v) ~= template[k] then
                error("Error: cfg parameter '" .. k .. "' should be of type " .. template[k])
            end
        else
            local good_types = string.gsub(template[k], ' ', '');
            if (string.find(',' .. good_types .. ',', ',' .. type(v) .. ',') == nil) then
                good_types = string.gsub(good_types, ',', ', ');
                error("Error: cfg parameter '" .. k .. "' should be one of types: " .. template[k])
            end
        end
        if wrapper_cfg[k] ~= nil then
            v = wrapper_cfg[k](v)
        end
        newtable[k] = v
    end
    return newtable
end

local function reload_cfg(oldcfg, newcfg)
    newcfg = prepare_cfg(newcfg)
    for key, val in pairs(newcfg) do
        if dynamic_cfg[key] == nil then
            box.error(box.error.RELOAD_CFG, key);
        end
        dynamic_cfg[key](val)
        rawset(oldcfg, key, val)
    end
    if type(box.on_reload_configuration) == 'function' then
        box.on_reload_configuration()
    end
end

local box = require('box')
-- Move all box members to box_saved
local box_configured = {}
for k, v in pairs(box) do
    box_configured[k] = v
    -- box.net.box uses box.error and box.internal
    if k ~= 'error' and k ~= 'internal' then
        box[k] = nil
    end
end

setmetatable(box, {
    __index = function(table, index)
        error(debug.traceback("Please call box.cfg{} first"))
        error("Please call box.cfg{} first")
     end
})

function box.cfg(cfg)
    cfg = prepare_cfg(cfg)
    for k,v in pairs(default_cfg) do
        if cfg[k] == nil then
            cfg[k] = v
        end
    end
    -- Restore box members from box_saved after initial configuration
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

    box.internal.snap_daemon.start()
end
jit.off(box.cfg)
jit.off(reload_cfg)
