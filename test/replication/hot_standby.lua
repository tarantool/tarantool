#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))
box.cfg({
    listen              = os.getenv("MASTER"),
    slab_alloc_arena    = 0.1,
    custom_proc_title   = "hot_standby",
    wal_dir             = "master",
    snap_dir            = "master",
})

