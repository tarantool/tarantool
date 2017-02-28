#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    pid_file            = "tarantool.pid",
    force_recovery      = true,
    rows_per_wal        = 10,
    snap_io_rate_limit  = 16
}

require('console').listen(os.getenv('ADMIN'))
