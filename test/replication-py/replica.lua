#!/usr/bin/env tarantool

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication_source  = os.getenv("MASTER"),
    slab_alloc_arena    = 0.1,
})

require('console').listen(os.getenv('ADMIN'))
