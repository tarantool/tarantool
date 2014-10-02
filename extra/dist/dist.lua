#!/usr/bin/env tarantool

--[[

=head1 NAME

dist.lua - an utility to control tarantool instances

=head1 SYNOPSIS

    vim /etc/tarantool/instances.enabled/my_instance.lua
    dist.lua start my_instance
    dist.lua stop  my_instance
    dist.lua logrotate my_instance

=head1 DESCRIPTION

The script is read C</etc/sysconfig/tarantool> or C</etc/default/tarantool>.
The file contains common default instances options:

    $ cat /etc/default/tarantool


    -- Options for Tarantool
    default_cfg = {
        -- will become pid_file .. instance .. '.pid'
        pid_file    =   "/var/run/tarantool",
        
        -- will become wal_dir/instance/
        wal_dir     =   "/var/lib/tarantool",
        
        -- snap_dir/instance/
        snap_dir    =   "/var/lib/tarantool",

        -- sophia_dir/instance/
        sophia_dir  =   "/var/lib/tarantool/sophia",
        
        -- logger/instance .. '.log'
        logger      =   "/var/log/tarantool",

        username    =   "tarantool",
    }

    instance_dir = "/etc/tarantool/instances.enabled"


The file defines C<instance_dir> where user can place his
applications (instances).

Each instance can be controlled by C<dist.lua>:

=head2 Starting instance

    dist.lua start instance_name

=head2 Stopping instance

    dist.lua stop instance_name

=head2 Logrotate instance's log

    dist.lua logrotate instance_name

=head2 Enter instance admin console

    dist.lua enter instance_name

=head2 status

    dist.lua status instance_name

Check if instance is up.

If pid file exists and control socket exists and control socket is alive
returns code C<0>.

Return code != 0 in other cases. Can complain in log (stderr) if pid file
exists and socket doesn't, etc.

=head1 COPYRIGHT

Copyright (C) 2010-2013 Tarantool AUTHORS:
please see AUTHORS file.



=cut

]]

local fio = require 'fio'
local log = require 'log'
local errno = require 'errno'
local yaml = require 'yaml'
local console = require 'console'
local socket = require 'socket'
local ffi = require 'ffi'
local os = require 'os'

ffi.cdef[[ int kill(int pid, int sig); ]]

if arg[1] == nil or arg[2] == nil then
    log.error("Usage: dist.lua {start|stop|logrotate} instance")
    os.exit(-1)
end

local cmd = arg[1]
local instance = fio.basename(arg[2], '.lua')

-- shift argv to remove 'tarantoolctl' from arg[0]
for i = 0, 128 do
    arg[i] = arg[i + 2]
    if arg[i] == nil then
        break
    end
end

if fio.stat('/etc/sysconfig/tarantool') then
    dofile('/etc/sysconfig/tarantool')
elseif fio.stat('/etc/default/tarantool') then
    dofile('/etc/default/tarantool')
end

if default_cfg == nil then
    default_cfg = {}
end

if instance_dir == nil then
    instance_dir = '/etc/tarantool/instances.enabled'
end

default_cfg.pid_file   = default_cfg.pid_file and default_cfg.pid_file or "/var/run/tarantool"
default_cfg.wal_dir    = default_cfg.wal_dir and default_cfg.wal_dir or "/var/lib/tarantool"
default_cfg.snap_dir   = default_cfg.snap_dir and default_cfg.snap_dir or "/var/lib/tarantool"
default_cfg.sophia_dir = default_cfg.sophia_dir and default_cfg.sophia_dir or "/var/lib/tarantool"
default_cfg.logger     = default_cfg.logger and default_cfg.logger or "/var/log/tarantool"
default_cfg.username   = default_cfg.username and default_cfg.username or "tarantool"

-- create  a path to the control socket (admin console)
local console_sock = fio.pathjoin(default_cfg.pid_file, instance .. '.control')

default_cfg.pid_file   = fio.pathjoin(default_cfg.pid_file, instance .. '.pid')
default_cfg.wal_dir    = fio.pathjoin(default_cfg.wal_dir, instance)
default_cfg.snap_dir   = fio.pathjoin(default_cfg.snap_dir, instance)
default_cfg.sophia_dir = fio.pathjoin(default_cfg.sophia_dir, instance, 'sophia')
default_cfg.logger     = fio.pathjoin(default_cfg.logger, instance .. '.log')

local instance_lua = fio.pathjoin(instance_dir, instance .. '.lua')

local function mkdir(dirname)
    log.info("mkdir %s", dirname)
    if not fio.mkdir(dirname, tonumber('0755', 8)) then
        log.error("Can't mkdir %s: %s", dirname, errno.strerror())
        os.exit(-1)
    end

    if not fio.chown(dirname, default_cfg.username, default_cfg.username) then
        log.error("Can't chown(%s, %s, %s): %s",
            default_cfg.username, default_cfg.username, dirname, errno.strerror())
    end
end

function mk_default_dirs(cfg)
    -- create pid_dir
    pid_dir = fio.dirname(cfg.pid_file)
    if fio.stat(pid_dir) == nil then
        mkdir(pid_dir)
    end
    -- create wal_dir 
    if fio.stat(cfg.wal_dir) == nil then
        mkdir(cfg.wal_dir)
    end
    -- create snap_dir 
    if fio.stat(cfg.snap_dir) == nil then
        mkdir(cfg.snap_dir)
    end
    -- create sophia_dir
    if fio.stat(cfg.sophia_dir) == nil then
        mkdir(cfg.sophia_dir)
    end
    -- create log_dir
    log_dir = fio.dirname(cfg.logger)
    if log_dir:find('|') == nil and fio.stat(log_dir) == nil then
        mkdir(log_dir)
    end
end

local force_cfg = {
    pid_file    = default_cfg.pid_file,
    username    = default_cfg.username,
    background  = true,
    custom_proc_title = instance
}

local orig_cfg = box.cfg
wrapper_cfg = function(cfg)

    for i, v in pairs(force_cfg) do
        cfg[i] = v
    end

    for i, v in pairs(default_cfg) do 
        if cfg[i] == nil then
            cfg[i] = v
        end
    end

    mk_default_dirs(cfg)
    local res = orig_cfg(cfg)

    require('fiber').name(instance)
    log.info('Run console at %s', console_sock)
    console.listen(console_sock)

    return res
end

if cmd == 'start' then
    box.cfg = wrapper_cfg
    dofile(instance_lua)

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
    if fio.stat(console_sock) == nil then
        -- process is not running, do nothing
        os.exit(0)
    end

    local s = socket.tcp_connect('unix/', console_sock)
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

elseif cmd == 'enter' then
    if fio.stat(console_sock) == nil then
        log.error("Can't connect to %s (socket not found)", console_sock)
        os.exit(-1)
    end
    
    log.info('Connecting to %s', console_sock)
    
    local cmd = string.format(
        "require('console').connect('%s')", console_sock)

    console.on_start( function(self) self:eval(cmd) end )
    console.on_client_disconnect( function(self) self.running = false end )
    console.start()
    os.exit(0)
elseif cmd == 'status' then
    if fio.stat(force_cfg.pid_file) == nil then
        if errno() == errno.ENOENT then
            os.exit(1)
        end
        log.error("Cant access pidfile %s: %s",
            force_cfg.pid_file, errno.strerror())
    end

    if fio.stat(console_sock) == nil then
        if errno() == errno.ENOENT then
            log.warn("pidfile is exists, but control socket (%s) isn't",
                console_sock)
            os.exit(2)
        end
    end

    local s = socket.tcp_connect('unix/', console_sock)
    if s == nil then
        if errno() ~= errno.EACCES then
            log.warn("Can't access control socket %s: %s", console_sock,
                errno.strerror())
            os.exit(3)
        else
            os.exit(0)
        end
    end

    s:close()
    os.exit(0)
else
    log.error("Unknown command '%s'", cmd)
    os.exit(-1)
end
