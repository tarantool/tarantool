#!/usr/bin/env tarantool
os = require('os')
box.cfg({
    listen              = os.getenv("MASTER"),
    admin               = os.getenv("ADMIN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    logger              = "tarantool.log",
    custom_proc_title   = "hot_standby",
    wal_dir             = "..",
    snap_dir            = "..",
})
