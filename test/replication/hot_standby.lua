#!/usr/bin/env tarantool
os = require('os')
box.cfg({
    primary_port        = os.getenv("MASTER_PORT"),
    admin_port          = os.getenv("ADMIN_PORT"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    logger              = "cat - >> tarantool.log",
    custom_proc_title   = "hot_standby",
    wal_dir             = "..",
    snap_dir            = "..",
})
