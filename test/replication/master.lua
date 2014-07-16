#!/usr/bin/env tarantool
os = require('os')
box.cfg({
    listen              = os.getenv("LISTEN"),
    admin               = os.getenv("ADMIN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    logger              = "| cat - >> tarantool.log",
    custom_proc_title   = "master",
})
