#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    wal_mode = 'none',
    memtx_memory = 2*1024*1024*1024,
    listen = os.getenv("LISTEN"),
    memtx_min_tuple_size=32,
    slab_alloc_factor=1.03
})
