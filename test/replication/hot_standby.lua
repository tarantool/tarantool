#!/usr/bin/env tarantool

box.cfg({
    listen              = os.getenv("MASTER"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    logger              = "tarantool.log",
    custom_proc_title   = "hot_standby",
    wal_dir             = "..",
    snap_dir            = "..",
})

require('console').listen(os.getenv('ADMIN'))
