#!/usr/bin/env tarantool

require('suite')

os.execute("rm -rf vinyl_test")
os.execute("mkdir -p vinyl_test")

local vinyl = {
    memory_limit = 0.1,
	threads = 5
}

box.cfg {
    listen            = os.getenv("LISTEN"),
    slab_alloc_arena  = 0.1,
    pid_file          = "tarantool.pid",
    rows_per_wal      = 500000,
    vinyl_dir        = "./vinyl_test",
    vinyl            = vinyl,
}

require('console').listen(os.getenv('ADMIN'))
