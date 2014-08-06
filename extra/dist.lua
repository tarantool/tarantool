#!env tarantool

local ffi  = require 'ffi'
local yaml = require 'yaml'

ffi.cdef[[
    int access(const char *path, int amode);
]]

local function file_exists(name)
    

end



local dist_cfg = {
    snap_dir = '/usr/lib/tarantool',
    xlog_dir = '/usr/lib/tarantool',
    log_dir  = '/var/log/tarantool',
    pid_dir  = '/var/run/tarantool',
    username = 'tarantool'
}





print(yaml.encode({arg, dist_cfg }))
