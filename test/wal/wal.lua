#!/usr/bin/env tarantool

box.cfg{
    listen              = os.getenv("LISTEN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    wal_mode            = "none"
}

require('console').listen(os.getenv('ADMIN'))
