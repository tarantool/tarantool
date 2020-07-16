#!/usr/bin/env tarantool

os.execute("rm -rf vinyl_test")
os.execute("mkdir -p vinyl_test")

box.cfg {
    listen            = os.getenv("LISTEN"),
    memtx_memory      = 107374182,
    pid_file          = "tarantool.pid",
    vinyl_dir         = "./vinyl_test",
    vinyl_memory      = 107374182,
    vinyl_read_threads = 3,
    vinyl_write_threads = 5,
    vinyl_range_size  = 1024 * 1024,
    vinyl_page_size   = 4 * 1024,
}

require('console').listen(os.getenv('ADMIN'))
