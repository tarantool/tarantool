#!/usr/bin/env tarantool

box.cfg({
    listen = os.getenv("LISTEN"),
    allocator = os.getenv("TEST_RUN_MEMTX_ALLOCATOR")
})

require('console').listen(os.getenv('ADMIN'))
