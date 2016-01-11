#!/usr/bin/env tarantool
box_cfg_done = false

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication_source  = os.getenv("MASTER"),
    slab_alloc_arena    = 0.1,
})

box_cfg_done = true
