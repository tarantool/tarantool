#!/usr/bin/env tarantool

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 157374182,
    pid_file            = "tarantool.pid",
    wal_mode            = "none",
    checkpoint_count    = 100
}

require('console').listen(os.getenv('ADMIN'))
