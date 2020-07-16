#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    pid_file            = "tarantool.pid",
    wal_max_size        = 2500,
    vinyl_read_threads  = 2,
    vinyl_write_threads = 3,
    vinyl_range_size    = 64 * 1024,
    vinyl_page_size     = 1024,
    memtx_max_tuple_size = 1024 * 1024 * 100,
    vinyl_max_tuple_size = 1024 * 1024 * 100,
}

require('console').listen(os.getenv('ADMIN'))
