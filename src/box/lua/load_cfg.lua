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
    snapshot_period     = 3600 * 4,
    snapshot_count      = 6,
}

-- types of available options
-- could be comma separated lua types or 'any' if any type is allowed
local template_cfg = {
    listen              = 'string, number',
    slab_alloc_arena    = 'number',
    slab_alloc_minimal  = 'number',
    slab_alloc_factor   = 'number',
    work_dir            = 'string',
    snap_dir            = 'string',
    wal_dir             = 'string',
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

local function reload_cfg(oldcfg, newcfg)
    if newcfg == nil then
        newcfg = {}
    end
    for key, val in pairs(newcfg) do
        if dynamic_cfg[key] == nil then
            box.error(box.error.RELOAD_CFG, key);
        end
        if val == "" then
            val = nil
        end
        if wrapper_cfg[key] ~= nil then
            val = wrapper_cfg[key](val)
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
    box[k] = nil
end

setmetatable(box, {
    __index = function(table, index)
        error("Please call box.cfg{} first")
     end
})

local function check_param_table(table, template)
    if table == nil then
        return
    end
    if type(table) ~= 'table' then
        error("Error: cfg should be a table")
    end
    -- just pass {.. dont_check = true, ..} to disable check below
    if table.dont_check then
        return
    end
    for k,v in pairs(table) do
        if template[k] == nil then
            error("Error: cfg parameter '" .. k .. "' is unexpected")
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
    end
end


local function update_param_table(table, defaults)
    if table == nil then
        table = {}
    end
    for k,v in pairs(defaults) do
        if table[k] == nil then
            table[k] = v
        end
    end
    return table
end

function box.cfg(cfg)
    check_param_table(cfg, template_cfg)
    cfg = update_param_table(cfg, default_cfg)

    for k,v in pairs(wrapper_cfg) do
        -- options that can be number or string
        cfg[k] = wrapper_cfg[k](cfg[k])
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
