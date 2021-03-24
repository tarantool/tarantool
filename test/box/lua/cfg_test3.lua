#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    memtx_memory = 214748364,
    vinyl_write_threads = 10,
    allocator = os.getenv("TEST_RUN_MEMTX_ALLOCATOR")
}

require('console').listen(os.getenv('ADMIN'))
box.schema.user.grant('guest', 'read,write,execute', 'universe')
