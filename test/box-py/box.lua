#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    pid_file            = "tarantool.pid",
    force_recovery      = true,
    wal_max_size        = 500
}

require('console').listen(os.getenv('ADMIN'))
