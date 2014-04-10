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
space = box.schema.create_space('tweedledum', { id = 0 })
space:create_index('primary', { type = 'hash'})
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
box.cfg{custom_proc_title="custom proc title"}
box.cfg{wal_dir="dynamic"}
box.cfg{snap_dir="dynamic"}
box.cfg{logger="new logger"}
-- bad1
box.cfg{slab_alloc_arena=0.2}
box.cfg.slab_alloc_arena

space:drop()
box.cfg{snap_io_rate_limit=0}
box.cfg{io_collect_interval=0}
box.cfg{too_long_threshold=0.5}
