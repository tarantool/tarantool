#!/usr/bin/env tarantool

os.execute("rm -rf vinyl_test")
os.execute("mkdir -p vinyl_test")

box.cfg {
    listen            = os.getenv("LISTEN"),
    memtx_memory      = 107374182,
    pid_file          = "tarantool.pid",
    vinyl_dir         = "./vinyl_test",
    vinyl_read_threads = 3,
    vinyl_write_threads = 5,
}

require('console').listen(os.getenv('ADMIN'))
