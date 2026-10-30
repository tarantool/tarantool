#!/usr/bin/env tarantool

box.cfg({
    listen          = os.getenv("LISTEN"),
    memtx_allocator = os.getenv("MEMTX_ALLOCATOR")
})

require('console').listen(os.getenv('ADMIN'))
