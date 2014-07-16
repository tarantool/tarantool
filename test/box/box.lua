#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    admin               = os.getenv("ADMIN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    rows_per_wal        = 50
}
