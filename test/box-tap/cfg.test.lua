#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('cfg')
local socket = require('socket')
local fio = require('fio')
test:plan(62)

--------------------------------------------------------------------------------
-- Invalid values
--------------------------------------------------------------------------------

test:is(type(box.cfg), 'function', 'box is not started')

local function invalid(name, val)
    local status, result = pcall(box.cfg, {[name]=val})
    test:ok(not status and result:match('Incorrect'), 'invalid '..name)
end

invalid('memtx_min_tuple_size', 7)
invalid('memtx_min_tuple_size', 0)
invalid('memtx_min_tuple_size', -1)
invalid('memtx_min_tuple_size', 1048281)
invalid('memtx_min_tuple_size', 1000000000)
invalid('replication', '//guest@localhost:3301')
invalid('wal_mode', 'invalid')
invalid('rows_per_wal', -1)
invalid('listen', '//!')
invalid('log', ':')
invalid('log', 'syslog:xxx=')

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

status, result = pcall(box.error, box.error.ILLEGAL_PARAMS, 'xx')
test:ok(result.code == box.error.ILLEGAL_PARAMS, "box.error without box.cfg")
status, result = pcall(function() return box.runtime.info() end)
test:ok(status and type(result) == 'table', "box.runtime without box.cfg")
status, result = pcall(function() return box.index.EQ end)
test:ok(status and type(result) == 'number', "box.index without box.cfg")
status, result = pcall(box.session.id)
test:ok(status, "box.session without box.cfg")
status, result = pcall(function() return box.sql end)
test:ok(not status and result:match('Please call box.cfg{}'),
	'exception on unconfigured box')

os.execute("rm -rf vinyl")
box.cfg{
    log="tarantool.log",
    memtx_memory=104857600,
    wal_mode = "", -- "" means default value
}

-- gh-678: vinyl engine creates vinyl dir with empty 'snapshot' file
test:isnil(io.open("vinyl", 'r'), 'vinyl_dir is not auto-created')

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

test:is(box.cfg.force_recovery, false, "force_recovery default value")
box.cfg{force_recovery=true}
test:is(box.cfg.force_recovery, true, "force_recovery new value")

test:is(box.cfg.wal_dir_rescan_delay, 2, "wal_dir_rescan_delay default value")
box.cfg{wal_dir_rescan_delay=0.2}
test:is(box.cfg.wal_dir_rescan_delay, 0.2, "wal_dir_rescan_delay new value")

test:is(box.cfg.too_long_threshold, 0.5, "too_long_threshold default value")
box.cfg{too_long_threshold=0.1}
test:is(box.cfg.too_long_threshold , 0.1, "too_long_threshold new value")

--------------------------------------------------------------------------------
-- gh-246: Read only mode
--------------------------------------------------------------------------------

test:is(box.cfg.read_only, false, "read_only default value")
box.cfg{read_only = true}
test:is(box.cfg.read_only, true, "read_only new value")
local status, reason = pcall(function()
    box.space._schema:insert({'read_only', 'test'})
end)
test:ok(not status and box.error.last().code == box.error.READONLY,
    "read_only = true")
box.cfg{read_only = false}
local status, reason = pcall(function()
    box.space._schema:insert({'read_only', 'test'})
end)
test:ok(status, "read_only = false")

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
code = [[ box.cfg{ log="tarantool.log", wal_mode = 'fsync' }; ]]
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

code = [[ box.cfg{ vinyl_dir='invalid' } ]]
test:is(run_script(code), PANIC, 'vinyl_dir is invalid')

code = [[ box.cfg{ memtx_dir='invalid' } ]]
test:is(run_script(code), PANIC, 'snap_dir is invalid')

code = [[ box.cfg{ wal_dir='invalid' } ]]
test:is(run_script(code), PANIC, 'wal_dir is invalid')

test:is(box.cfg.log_nonblock, true, "log_nonblock default value")
code = [[
box.cfg{log_nonblock = false }
os.exit(box.cfg.log_nonblock == false and 0 or 1)
]]
test:is(run_script(code), 0, "log_nonblock new value")

-- box.cfg { listen = xx }
local path = './tarantool.sock'
os.remove(path)
box.cfg{ listen = 'unix/:'..path }
local s = socket.tcp_connect('unix/', path)
test:isnt(s, nil, "dynamic listen")
if s then s:close() end
box.cfg{ listen = '' }
s = socket.tcp_connect('unix/', path)
test:isnil(s, 'dynamic listen')
if s then s:close() end
os.remove(path)

path = './tarantool.sock'
local path2 = './tarantool2.sock'
local s = socket.tcp_server('unix/', path, function () end)
os.execute('ln ' .. path .. ' ' .. path2)
s:close()
box.cfg{ listen = 'unix/:'.. path2}
s = socket.tcp_connect('unix/', path2)
test:isnt(s, nil, "reuse unix socket")
if s then s:close() end
box.cfg{ listen = '' }
os.remove(path2)

code = " box.cfg{ listen='unix/:'" .. path .. "' } "
run_script(code)
test:isnil(fio.stat(path), "delete socket at exit")

--
-- gh-1499: AUTH raises ER_LOADING if wal_mode is 'none'
--
code = [[
box.cfg{wal_mode = 'none', listen='unix/:./tarantool.sock' }
box.once("bootstrap", function()
    box.schema.user.create("test", { password = '123'  })
end)
local conn = require('net.box').connect('unix/:./tarantool.sock',
    { user = 'test', password = '123' })
if not conn:ping() then os.exit(1) end
os.exit(0)
]]
test:is(run_script(code), 0, "wal_mode none and ER_LOADING")

--
-- gh-1962: incorrect replication source
--
status, reason = pcall(box.cfg, {replication="3303,3304"})
test:ok(not status and reason:match("Incorrect"), "invalid replication")

--
-- gh-1778 vinyl page can't be greather than range
--
code = [[
box.cfg{vinyl_page_size = 4 * 1024 * 1024, vinyl_range_size = 2 * 1024 * 1024}
os.exit(0)
]]
test:is(run_script(code), PANIC, "page size greather than range")

code = [[
box.cfg{vinyl_page_size = 1 * 1024 * 1024, vinyl_range_size = 2 * 1024 * 1024}
os.exit(0)
]]
test:is(run_script(code), 0, "page size less than range")

code = [[
box.cfg{vinyl_page_size = 2 * 1024 * 1024, vinyl_range_size = 2 * 1024 * 1024}
os.exit(0)
]]
test:is(run_script(code), 0, "page size equal with range")

--
-- gh-2150 one vinyl worker thread is reserved for dumps
--
code = [[
box.cfg{vinyl_threads=1}
os.exit(0)
]]
test:is(run_script(code), PANIC, "vinyl_threads = 1")

code = [[
box.cfg{vinyl_threads=2}
os.exit(0)
]]
test:is(run_script(code), 0, "vinyl_threads = 2")

-- test memtx options upgrade
code = [[
box.cfg{slab_alloc_arena = 0.2, slab_alloc_minimal = 16,
    slab_alloc_maximal = 64 * 1024}
os.exit(box.cfg.memtx_memory == 214748364 
    and box.cfg.memtx_min_tuple_size == 16
    and box.cfg.memtx_max_tuple_size == 64 * 1024
and 0 or 1)
]]
test:is(run_script(code), 0, "upgrade memtx memory options")

code = [[
box.cfg{slab_alloc_arena = 0.2, slab_alloc_minimal = 16, slab_alloc_maximal = 64 * 1024,
    memtx_memory = 214748364, memtx_min_tuple_size = 16,
    memtx_max_tuple_size = 64 * 1024}
os.exit(0)
]]
test:is(run_script(code), 0, "equal new and old memtx options")

code = [[
box.cfg{slab_alloc_arena = 0.2, slab_alloc_minimal = 16, slab_alloc_maximal = 64 * 1024,
    memtx_memory = 107374182, memtx_min_tuple_size = 16,
    memtx_max_tuple_size = 64 * 1024}
os.exit(0)
]]
test:is(run_script(code), PANIC, "different new and old memtx_memory")

code = [[
box.cfg{slab_alloc_arena = 0.2, slab_alloc_minimal = 16, slab_alloc_maximal = 64 * 1024,
    memtx_memory = 214748364, memtx_min_tuple_size = 32,
    memtx_max_tuple_size = 64 * 1024}
os.exit(0)
]]
test:is(run_script(code), PANIC, "different new and old min_tuple_size")

code = [[
box.cfg{snap_dir = 'tmp1', memtx_dir = 'tmp2'}
os.exit(0)
]]
test:is(run_script(code), PANIC, "different memtx_dir")

code = [[
box.cfg{panic_on_wal_error = true}
os.exit(box.cfg.force_recovery == false and 0 or 1)
]]
test:is(run_script(code), 0, "panic_on_wal_error")

code = [[
box.cfg{panic_on_snap_error = false}
os.exit(box.cfg.force_recovery == true and 0 or 1)
]]
test:is(run_script(code), 0, "panic_on_snap_error")

code = [[
box.cfg{snapshot_period = 100, snapshot_count = 4}
os.exit(box.cfg.checkpoint_interval == 100
      and box.cfg.checkpoint_count == 4 and 0 or 1)
]]
test:is(run_script(code), 0, "setup checkpoint params")

code = [[
box.cfg{snapshot_period = 100, snapshot_count = 4}
box.cfg{snapshot_period = 150, snapshot_count = 8}
os.exit(box.cfg.checkpoint_interval == 150
      and box.cfg.checkpoint_count == 8 and 0 or 1)
]]
test:is(run_script(code), 0, "update checkpoint params")

--
--  test wal_max_size option
--
code = [[
digest = require'digest'
fio = require'fio'
box.cfg{wal_max_size = 1024}
_ = box.schema.space.create('test'):create_index('pk')
data = digest.urandom(1024)
cnt1 = #fio.glob(fio.pathjoin(box.cfg.wal_dir, '*.xlog'))
for i = 0, 9 do
  box.space.test:replace({1, data})
end
cnt2 = #fio.glob(fio.pathjoin(box.cfg.wal_dir, '*.xlog'))
os.exit(cnt1 < cnt2 - 8 and 0 or 1)
]]
test:is(run_script(code), 0, "wal_max_size xlog rotation")

test:check()
os.exit(0)
