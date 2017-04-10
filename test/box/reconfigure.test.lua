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
