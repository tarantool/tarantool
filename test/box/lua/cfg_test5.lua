#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    vinyl_memory = 1024 * 1024 * 1024,
    allocator = os.getenv("TEST_RUN_MEMTX_ALLOCATOR")
}

require('console').listen(os.getenv('ADMIN'))
box.schema.user.grant('guest', 'read,write,execute', 'universe')