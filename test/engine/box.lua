#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    vinyl_memory        = 107374182,
    pid_file            = "tarantool.pid",
    wal_max_size        = 2500,
    vinyl_read_threads  = 3,
    vinyl_write_threads = 5,
    vinyl_range_size    = 1024 * 1024,
    vinyl_page_size     = 4 * 1024,
    memtx_max_tuple_size = 1024 * 1024 * 100,
    vinyl_max_tuple_size = 1024 * 1024 * 100,
    memtx_allocator      = os.getenv("MEMTX_ALLOCATOR"),
}

require('console').listen(os.getenv('ADMIN'))
