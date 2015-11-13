#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    panic_on_wal_error  = false,
    rows_per_wal        = 10
}

require('console').listen(os.getenv('ADMIN'))
