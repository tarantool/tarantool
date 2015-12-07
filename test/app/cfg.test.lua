#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('cfg')
local socket = require('socket')
local fio = require('fio')
test:plan(30)

--------------------------------------------------------------------------------
-- Invalid values
--------------------------------------------------------------------------------

test:is(type(box.cfg), 'function', 'box is not started')

local function invalid(name, val)
    local status, result = pcall(box.cfg, {[name]=val})
    test:ok(not status and result:match('Incorrect'), 'invalid '..name)
end

invalid('replication_source', '//guest@localhost:3301')
invalid('wal_mode', 'invalid')
invalid('rows_per_wal', -1)
invalid('listen', '//!')
invalid('logger', ':')
invalid('logger', 'syslog:xxx=')

test:is(type(box.cfg), 'function', 'box is not started')

--------------------------------------------------------------------------------
-- All box members must raise an exception on access if box.cfg{} wasn't called
--------------------------------------------------------------------------------

local box = require('box')
local function testfun()
    return type(box.space)
end

local status, result = pcall(testfun)

test:ok(not status and result:match('Please call box.cfg{}'),
    'exception on unconfigured box')

os.execute("rm -rf sophia")
box.cfg{
    logger="tarantool.log",
    slab_alloc_arena=0.1,
    wal_mode = "", -- "" means default value
}

-- gh-678: sophia engine creates sophia dir with empty 'snapshot' file
test:isnil(io.open("sophia", 'r'), 'sophia_dir is not auto-created')

status, result = pcall(testfun)
test:ok(status and result == 'table', 'configured box')

--------------------------------------------------------------------------------
-- gh-534: Segmentation fault after two bad wal_mode settings
--------------------------------------------------------------------------------

test:is(box.cfg.wal_mode, "write", "wal_mode default value")
-- box.cfg{wal_mode = ""}
-- test:is(box.cfg.wal_mode, "write", "wal_mode default value")
-- box.cfg{wal_mode = "none"}
-- test:is(box.cfg.wal_mode, "none", "wal_mode change")
-- "" or NULL resets option to default value
-- box.cfg{wal_mode = ""}
-- test:is(box.cfg.wal_mode, "write", "wal_mode default value")
-- box.cfg{wal_mode = "none"}
-- test:is(box.cfg.wal_mode, "none", "wal_mode change")
-- box.cfg{wal_mode = require('msgpack').NULL}
-- test:is(box.cfg.wal_mode, "write", "wal_mode default value")

test:is(box.cfg.panic_on_wal_error, true, "panic_on_wal_mode default value")
box.cfg{panic_on_wal_error=false}
test:is(box.cfg.panic_on_wal_error, false, "panic_on_wal_mode new value")

test:is(box.cfg.wal_dir_rescan_delay, 2, "wal_dir_rescan_delay default value")
box.cfg{wal_dir_rescan_delay=0.2}
test:is(box.cfg.wal_dir_rescan_delay, 0.2, "wal_dir_rescan_delay new value")

test:is(box.cfg.too_long_threshold, 0.5, "too_long_threshold default value")
box.cfg{too_long_threshold=0.1}
test:is(box.cfg.too_long_threshold , 0.1, "too_long_threshold new value")

local tarantool_bin = arg[-1]
local PANIC = 256
function run_script(code)
    local dir = fio.tempdir()
    local script_path = fio.pathjoin(dir, 'script.lua')
    local script = fio.open(script_path, {'O_CREAT', 'O_WRONLY', 'O_APPEND'},
        tonumber('0777', 8))
    script:write(code)
    script:write("\nos.exit(0)")
    script:close()
    local cmd = [[/bin/sh -c 'cd "%s" && "%s" ./script.lua 2> /dev/null']]
    local res = os.execute(string.format(cmd, dir, tarantool_bin))
    fio.rmdir(dir)
    return res
end

-- gh-715: Cannot switch to/from 'fsync'
code = [[ box.cfg{ logger="tarantool.log", wal_mode = 'fsync' }; ]]
test:is(run_script(code), 0, 'wal_mode fsync')

code = [[ box.cfg{ wal_mode = 'fsync' }; box.cfg { wal_mode = 'fsync' }; ]]
test:is(run_script(code), 0, 'wal_mode fsync -> fsync')

code = [[ box.cfg{ wal_mode = 'fsync' }; box.cfg { wal_mode = 'none'} ]]
test:is(run_script(code), PANIC, 'wal_mode fsync -> write is not supported')

code = [[ box.cfg{ wal_mode = 'write' }; box.cfg { wal_mode = 'fsync'} ]]
test:is(run_script(code), PANIC, 'wal_mode write -> fsync is not supported')

-- gh-684: Inconsistency with box.cfg and directories
local code;
code = [[ box.cfg{ work_dir='invalid' } ]]
test:is(run_script(code), PANIC, 'work_dir is invalid')

code = [[ box.cfg{ sophia_dir='invalid' } ]]
test:is(run_script(code), PANIC, 'sophia_dir is invalid')

code = [[ box.cfg{ snap_dir='invalid' } ]]
test:is(run_script(code), PANIC, 'snap_dir is invalid')

code = [[ box.cfg{ wal_dir='invalid' } ]]
test:is(run_script(code), PANIC, 'wal_dir is invalid')

test:is(box.cfg.logger_nonblock, true, "logger_nonblock default value")
code = [[
box.cfg{logger_nonblock = false }
os.exit(box.cfg.logger_nonblock == false and 0 or 1)
]]
test:is(run_script(code), 0, "logger_nonblock new value")

-- box.cfg { listen = xx }
local path = './tarantool.sock'
os.remove(path)
box.cfg{ listen = 'unix/:'..path }
s = socket.tcp_connect('unix/', path)
test:isnt(s, nil, "dynamic listen")
if s then s:close() end
box.cfg{ listen = '' }
s = socket.tcp_connect('unix/', path)
test:isnil(s, 'dynamic listen')
if s then s:close() end
os.remove(path)

test:check()
os.exit(0)
