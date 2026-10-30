#!/usr/bin/env tarantool

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 157374182,
    pid_file            = "tarantool.pid",
    wal_mode            = "none",
    checkpoint_count    = 100
}

-- Wal off tests can be running for a very long time without yields.
require('fiber').set_max_slice(100500)
require('console').listen(os.getenv('ADMIN'))
