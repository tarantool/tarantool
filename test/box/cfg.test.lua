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

-- check that cfg with unexpected type of parameter failes
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
box.cfg{vinyl = "vinyl"}
box.cfg{vinyl_write_threads = "threads"}


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

test_run:cmd("clear filter")
