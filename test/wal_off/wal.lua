#!/usr/bin/env tarantool

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 107374182,
    pid_file            = "tarantool.pid",
    wal_mode            = "none"
}

require('console').listen(os.getenv('ADMIN'))
