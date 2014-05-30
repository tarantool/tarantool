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


local function normalize_port_uri(port)
    if port == nil then
        return nil
    end
    return tostring(port);
end

-- arguments that can be number or string
local wrapper_cfg = {
    admin_port          = normalize_port_uri,
    primary_port        = normalize_port_uri,
}

-- all available options
local default_cfg = {
    admin_port          = nil,
    primary_port        = nil,
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
}

-- dynamically settable options
local dynamic_cfg = {
    wal_mode                = ffi.C.box_set_wal_mode,
    replication_source      = ffi.C.box_set_replication_source,
    log_level               = ffi.C.box_set_log_level,
    io_collect_interval     = ffi.C.box_set_io_collect_interval,
    too_long_threshold      = ffi.C.box_set_too_long_threshold,
    snap_io_rate_limit      = ffi.C.box_set_snap_io_rate_limit,
}

local function reload_cfg(oldcfg, newcfg)
    if newcfg == nil then
        newcfg = {}
    end
    for key, val in pairs(newcfg) do
        if dynamic_cfg[key] == nil then
            box.raise(box.error.ER_RELOAD_CFG,
            "Can't set option '"..key.."' dynamically");
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

function box.cfg(cfg)
    if cfg == nil then
        cfg = {}
    end
    for k,v in pairs(default_cfg) do
        if cfg[k] == nil then
            cfg[k] = v
        end
    end

    for k,v in pairs(wrapper_cfg) do
        -- options that can be number or string
        cfg[k] = wrapper_cfg[k](cfg[k])
    end
    box.cfg = setmetatable(cfg,
        {
		    __newindex = function(table, index)
		        error('Attempt to modify a read-only table')
		    end,
            __call = reload_cfg
        })
    ffi.C.load_cfg()
end
jit.off(box.cfg)
jit.off(reload_cfg)
