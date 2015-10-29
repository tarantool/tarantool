#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    slab_alloc_arena    = 0.01,
    pid_file            = "tarantool.pid",
    panic_on_wal_error  = true,
    panic_on_snap_error  = true,
    rows_per_wal        = 5000000
}

require('console').listen(os.getenv('ADMIN'))
box.schema.user.grant('guest', 'read,write,execute', 'universe')
