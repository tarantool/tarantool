#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    allocator = os.getenv("TEST_RUN_MEMTX_ALLOCATOR")
}

require('console').listen(os.getenv('ADMIN'))
box.schema.user.grant('guest', 'read,write,execute', 'universe')
