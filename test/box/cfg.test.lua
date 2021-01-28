env = require('test_run')
test_run = env.new()
test_run:cmd("push filter '(error: .*)\\.lua:[0-9]+: ' to '\\1.lua:<line>: '")
box.cfg.nosuchoption = 1
cfg_filter(box.cfg)
-- must be read-only
box.cfg()
cfg_filter(box.cfg)

-- check that cfg with unexpected parameter fails.
box.cfg{sherlock = 'holmes'}

-- check that cfg with unexpected type of parameter fails
box.cfg{listen = {}}
box.cfg{wal_dir = 0}
box.cfg{coredump = 'true'}

-- check comment to issue #2191 - bad argument #2 to ''uri_parse''
box.cfg{replication = {}}
box.cfg{replication = {}}

--------------------------------------------------------------------------------
-- Test of hierarchical cfg type check
--------------------------------------------------------------------------------

box.cfg{memtx_memory = "100500"}
box.cfg{memtx_memory = -1}
box.cfg{vinyl_memory = -1}
box.cfg{vinyl = "vinyl"}
box.cfg{vinyl_write_threads = "threads"}
--
-- gh-4705: too big memory size led to an assertion.
--
box.cfg{memtx_memory = 5000000000000}
box.cfg{vinyl_memory = 5000000000000}

--------------------------------------------------------------------------------
-- Dynamic configuration check
--------------------------------------------------------------------------------

replication_sync_lag = box.cfg.replication_sync_lag
box.cfg{replication_sync_lag = 0.123}
box.cfg.replication_sync_lag
box.cfg{replication_sync_lag = replication_sync_lag}

replication_sync_timeout = box.cfg.replication_sync_timeout
box.cfg{replication_sync_timeout = 123}
box.cfg.replication_sync_timeout
box.cfg{replication_sync_timeout = replication_sync_timeout}

box.cfg{instance_uuid = box.info.uuid}
box.cfg{instance_uuid = '12345678-0123-5678-1234-abcdefabcdef'}

box.cfg{replicaset_uuid = box.info.cluster.uuid}
box.cfg{replicaset_uuid = '12345678-0123-5678-1234-abcdefabcdef'}

box.cfg{memtx_memory = box.cfg.memtx_memory}
box.cfg{vinyl_memory = box.cfg.vinyl_memory}
box.cfg{sql_cache_size = box.cfg.sql_cache_size}

--------------------------------------------------------------------------------
-- Test of default cfg options
--------------------------------------------------------------------------------

test_run:cmd('create server cfg_tester1 with script = "box/lua/cfg_test1.lua"')
test_run:cmd("start server cfg_tester1")
test_run:cmd('switch cfg_tester1')
box.cfg.memtx_memory, box.cfg.slab_alloc_factor, box.cfg.vinyl_write_threads
test_run:cmd("switch default")
test_run:cmd("stop server cfg_tester1")
test_run:cmd("cleanup server cfg_tester1")

test_run:cmd('create server cfg_tester2 with script = "box/lua/cfg_test2.lua"')
test_run:cmd("start server cfg_tester2")
test_run:cmd('switch cfg_tester2')
box.cfg.memtx_memory, box.cfg.slab_alloc_factor, box.cfg.vinyl_write_threads
test_run:cmd("switch default")
test_run:cmd("stop server cfg_tester2")
test_run:cmd("cleanup server cfg_tester2")

test_run:cmd('create server cfg_tester3 with script = "box/lua/cfg_test3.lua"')
test_run:cmd("start server cfg_tester3")
test_run:cmd('switch cfg_tester3')
box.cfg.memtx_memory, box.cfg.slab_alloc_factor, box.cfg.vinyl_write_threads
test_run:cmd("switch default")
test_run:cmd("stop server cfg_tester3")
test_run:cmd("cleanup server cfg_tester3")

test_run:cmd('create server cfg_tester4 with script = "box/lua/cfg_test4.lua"')
test_run:cmd("start server cfg_tester4")
test_run:cmd('switch cfg_tester4')
box.cfg.memtx_memory, box.cfg.slab_alloc_factor, box.cfg.vinyl_write_threads
test_run:cmd("switch default")
test_run:cmd("stop server cfg_tester4")
test_run:cmd("cleanup server cfg_tester4")

--------------------------------------------------------------------------------
-- Check fix for pid_file option overwritten by tarantoolctl
--------------------------------------------------------------------------------

test_run:cmd('create server cfg_tester5 with script = "box/lua/cfg_test1.lua"')
test_run:cmd("start server cfg_tester5")
test_run:cmd('switch cfg_tester5')
box.cfg{pid_file = "current.pid"}
test_run:cmd("switch default")
test_run:cmd("stop server cfg_tester5")
test_run:cmd("cleanup server cfg_tester5")

--------------------------------------------------------------------------------
-- Check that 'vinyl_dir' cfg option is not checked as long as
-- there is no vinyl indexes (issue #2664)
--------------------------------------------------------------------------------

test_run:cmd('create server cfg_tester with script = "box/lua/cfg_bad_vinyl_dir.lua"')
test_run:cmd("start server cfg_tester")
test_run:cmd('switch cfg_tester')
_ = box.schema.space.create('test_memtx', {engine = 'memtx'})
_ = box.space.test_memtx:create_index('pk') -- ok
_ = box.schema.space.create('test_vinyl', {engine = 'vinyl'})
_ = box.space.test_vinyl:create_index('pk') -- error
box.snapshot()
test_run:cmd("restart server cfg_tester")
test_run:cmd("switch default")
test_run:cmd("stop server cfg_tester")
test_run:cmd("cleanup server cfg_tester")

--
-- gh-3320: box.cfg{net_msg_max}.
--
box.cfg{net_msg_max = 'invalid'}
--
-- gh-3425: incorrect error message: must not contain 'iproto'.
--
box.cfg{net_msg_max = 0}
old = box.cfg.net_msg_max
box.cfg{net_msg_max = 2}
box.cfg{net_msg_max = old + 1000}
box.cfg{net_msg_max = old}

test_run:cmd("clear filter")

--
-- gh-4236: initial box.cfg{} call did not log changes to default state
--
test_run:cmd('create server cfg_tester6 with script = "box/lua/cfg_test5.lua"')
test_run:cmd("start server cfg_tester6")
test_run:grep_log('cfg_tester6', 'set \'vinyl_memory\' configuration option to 1073741824', 1000)
test_run:cmd("stop server cfg_tester6")
test_run:cmd("cleanup server cfg_tester6")

--
-- gh-4493: Replication user password may leak to logs
--
test_run:cmd('create server cfg_tester7 with script = "box/lua/cfg_test6.lua"')
test_run:cmd("start server cfg_tester7")
-- test there is replication log in log
test_run:grep_log('cfg_tester7', 'set \'replication\' configuration option to', 1000)
-- test there is no password in log
test_run:grep_log('cfg_tester7', 'test%-cluster%-cookie', 1000)
test_run:cmd("stop server cfg_tester7")
test_run:cmd("cleanup server cfg_tester7")

--
-- gh-4574: Check schema version after Tarantool update.
--
test_run:cmd('create server cfg_tester8 with script = "box/lua/cfg_test8.lua", workdir="sql/upgrade/2.1.0/"')
test_run:cmd("start server cfg_tester8")
--- Check that the warning is printed.
version_warning = "Please, consider using box.schema.upgrade()."
test_run:wait_log('cfg_tester8', version_warning, 1000, 1.0) ~= nil
test_run:cmd("stop server cfg_tester8")
test_run:cmd("cleanup server cfg_tester8")

test_run:cmd('create server cfg_tester9 with script = "box/lua/cfg_test9.lua"')
-- slab_alloc_factor == 3.14 is invalid, because it must be less than or equal to 2.0
test_run:cmd("start server cfg_tester9 with crash_expected=True")
test_run:cmd("cleanup server cfg_tester9")

test_run:cmd('create server cfg_tester10 with script = "box/lua/cfg_test1.lua"')
test_run:cmd("start server cfg_tester10")
--- Check that the warning isn't printed.
test_run:wait_log('cfg_tester10', version_warning, 1000, 1.0) == nil
test_run:cmd("stop server cfg_tester10")
test_run:cmd("cleanup server cfg_tester10")
