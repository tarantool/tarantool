#!env tarantool

local fio = require 'fio'
local log = require 'log'
local errno = require 'errno'
local yaml = require 'yaml'
local console = require 'console'
local socket = require 'socket'
local ffi = require 'ffi'

ffi.cdef[[ int kill(int pid, int sig); ]]

local DEFAULTS  = '/etc/sysconfig/tarantool'
local DEFAULT_CFG = {
    PIDS        = '/var/pid/tarantool',
    SNAPS       = '/var/lib/tarantool',
    XLOGS       = '/var/lib/tarantool',
    LOGS        = '/var/log/tarantool',
    USERNAME    = 'tarantool',
    INSTDIR     = '/etc/tarantool/instances.enabled',
}

if fio.stat(DEFAULTS) == nil then
    DEFAULTS = '/etc/default/tarantool'
end

function read_cfg(name)
    if name == nil then
        return {}
    end
    local stat = fio.stat(name)
    if stat == nil then
        log.error("Can't stat file %s: %s", name, errno.strerror())
        return {}
    end

    local f = fio.open(name, 'O_RDONLY')
    if f == nil then
        log.error("Can't open file %s: %s", name, errno.strerror())
        return {}
    end

    local data = f:read(32768)
    if data == nil then
        log.error("Can't read file %s: %s", name, errno.strerror())
        f:close()
        return {}
    end
    f:close()

    local result = {}
    repeat
        local line
        if string.match(data, "\n") == nil then
            line = data
            data = ''
        else
            line = string.match(data, "^(.-)\n")
            data = string.sub(data, #line + 1 + 1)
        end


        local name, value = string.match(line, "^%s*(.-)%s*=%s*(.-)%s*$")

        if name ~= nil and value ~= nil and #name > 0 and #value > 0 then
            if string.match(value, '^".*"$') ~= nil then
                value = string.sub(value, 2, #value - 2)
            elseif string.match(value, "^'.*'$") ~= nil then
                value = string.sub(value, 2, #value - 2)
            end

            if string.match(name, '^%s*#') == nil then
                result[name] = value
            end
        end

    until #data == 0

    return result
end

if arg[1] == nil or arg[2] == nil then
    log.error("Usage: dist.lua {start|stop|logrotate} instance")
    os.exit(-1)
end


local cfg = read_cfg(DEFAULTS)
for i, v in pairs(DEFAULT_CFG) do
    if cfg[i] == nil then
        cfg[i] = v
    end
end

local function mkdir(dirname)
    log.info("mkdir %s", dirname)
    if not fio.mkdir(dirname, 0x1C0) then
        log.error("Can't mkdir %s: %s", dirname, errno.strerror())
        os.exit(-1)
    end

    if not fio.chown(dirname, cfg.USERNAME, cfg.USERNAME) then
        log.error("Can't chown(%s, %s, %s): %s",
            cfg.USERNAME, cfg.USERNAME, dirname, errno.strerror())
    end
end

local cmd = arg[1]
local instance = fio.basename(arg[2], '.lua')
local main_lua = fio.pathjoin(cfg.INSTDIR, instance .. '.lua')
for i = 0, 128 do
    arg[i] = arg[i + 2]
    if arg[i] == nil then
        break
    end
end

local force_cfg_console = fio.pathjoin(cfg.PIDS, instance .. '.control')

local force_cfg = {
    pid_file    = fio.pathjoin(cfg.PIDS, instance .. '.pid'),
    wal_dir     = fio.pathjoin(cfg.XLOGS, instance),
    work_dir    = fio.pathjoin(cfg.XLOGS, instance),
    snap_dir    = fio.pathjoin(cfg.SNAPS, instance),
    username    = cfg.USERNAME,
    logger      = fio.pathjoin(cfg.LOGS, instance .. '.log'),
    background  = true,
    custom_proc_title = instance
}



local orig_cfg = box.cfg
box.cfg = function(cfg)
    for i, v in pairs(force_cfg) do
        cfg[i] = v
    end
    local res = orig_cfg(cfg)

    require('fiber').name(instance)
    log.info('Run console at %s', force_cfg_console)
    console.listen(force_cfg_console)

    return res
end


if cmd == 'start' then
    -- create PIDDIR
    if fio.stat(cfg.PIDS) == nil then
        mkdir(cfg.PIDS)
    end

    -- create xlogdir
    if fio.stat(force_cfg.wal_dir) == nil then
        mkdir(force_cfg.wal_dir)
    end

    -- create snapdir
    if fio.stat(force_cfg.snap_dir) == nil then
        mkdir(force_cfg.snap_dir)
    end
    dofile(main_lua)

elseif cmd == 'stop' then
    if fio.stat(force_cfg.pid_file) == nil then
        log.error("Process is not running (pid: %s)", force_cfg.pid_file)
        os.exit(-1)
    end

    local f = fio.open(force_cfg.pid_file, 'O_RDONLY')
    if f == nil then
        log.error("Can't read pid file %s: %s",
            force_cfg.pid_file, errno.strerror())
    end

    local str = f:read(64)
    f:close()

    local pid = tonumber(str)

    if pid == nil or pid <= 0 then
        log.error("Broken pid file %s", force_cfg.pid_file)
        fio.unlink(force_cfg.pid_file)
        os.exit(-1)
    end

    if ffi.C.kill(pid, 15) < 0 then
        log.error("Can't kill process %d: %s", pid, errno.strerror())
        fio.unlink(force_cfg.pid_file)
    end
    os.exit(-1)

elseif cmd == 'logrotate' then
    if fio.stat(force_cfg.console) == nil then
        -- process is not running, do nothing
        os.exit(0)
    end

    local s = socket.tcp_connect('unix/', force_cfg.console)
    if s == nil then
        -- socket is not opened, do nothing
        os.exit(0)
    end

    s:write[[
        require('log'):rotate()
        require('log').info("Rotate log file")
    ]]

    s:read({ '[.][.][.]' }, 2)

    os.exit(0)
else
    log.error("Unknown command '%s'", cmd)
    os.exit(-1)
end
