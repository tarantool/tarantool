#!/usr/bin/env tarantool
local os = require('os')

box.cfg{
    listen = os.getenv("LISTEN"),
    read_only = true,
    allocator = os.getenv("TEST_RUN_MEMTX_ALLOCATOR")
}

require('console').listen(os.getenv('ADMIN'))
