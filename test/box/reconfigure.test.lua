test_run = require('test_run').new()

too_long_threshold_default = box.cfg.too_long_threshold
io_collect_interval_default = box.cfg.io_collect_interval

box.cfg.too_long_threshold
-- good
box.cfg{too_long_threshold=0.2}
box.cfg.too_long_threshold
-- good 
box.cfg{snap_io_rate_limit=10}
box.cfg.snap_io_rate_limit
box.cfg.io_collect_interval
box.cfg{io_collect_interval=0.001}
box.cfg.io_collect_interval

-- A test case for http://bugs.launchpad.net/bugs/712447:
-- Valgrind reports use of not initialized memory after 'reload
-- configuration'
--
space = box.schema.space.create('tweedledum')
index = space:create_index('primary')
space:insert{1, 'tuple'}
box.snapshot()
box.cfg{}

space:insert{2, 'tuple2'}
box.snapshot()
space:insert{3, 'tuple3'}
box.snapshot()

-- A test case for https://github.com/tarantool/tarantool/issues/112:
-- Tarantool crashes with SIGSEGV during reload configuration
--
-- log level
box.cfg{log_level=5}
-- constants
box.cfg{wal_dir="dynamic"}
box.cfg{memtx_dir="dynamic"}
box.cfg{log="new logger"}
-- bad1
box.cfg{memtx_memory=53687091}
box.cfg.memtx_memory

space:drop()
box.cfg{snap_io_rate_limit=0}
box.cfg{io_collect_interval=0}
box.cfg{too_long_threshold=0.5}
box.cfg.snap_io_rate_limit = nil
box.cfg.io_collect_interval = nil

box.cfg { too_long_threshold = too_long_threshold_default }
box.cfg { io_collect_interval = io_collect_interval_default }

--
-- gh-2634: check that box.cfg.memtx_memory can be increased
--
test_run:cmd("create server test with script='box/lua/cfg_memory.lua'")
test_run:cmd(string.format("start server test with args='%d'", 48 * 1024 * 1024))
test_run:cmd("switch test")

box.slab.info().quota_size

s = box.schema.space.create('test')
_ = s:create_index('pk')
count = 200
pad = string.rep('x', 100 * 1024)
for i = 1, count do s:replace{i, pad} end -- error: not enough memory
s:count() < count

box.cfg{memtx_memory = 64 * 1024 * 1024}
box.slab.info().quota_size

for i = s:count() + 1, count do s:replace{i, pad} end -- ok
s:count() == count
s:drop()

box.cfg{memtx_memory = 48 * 1024 * 1024} -- error: decreasing memtx_memory is not allowed
box.slab.info().quota_size

test_run:cmd("switch default")
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
