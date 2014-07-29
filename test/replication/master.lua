#!/usr/bin/env tarantool
os = require('os')
box.cfg({
    listen              = os.getenv("LISTEN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    logger              = "| cat - >> tarantool.log",
    custom_proc_title   = "master",
})

require('console').listen(os.getenv('ADMIN'))
