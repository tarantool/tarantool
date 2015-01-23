#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('cfg')
test:plan(1)

os.execute("rm -rf sophia")
local box = require('box')
box.cfg{
    logger="tarantool.log",
    slab_alloc_arena=0.1,
    wal_mode = ""
}
test:isnil(io.open("sophia", 'r'), 'sophia_dir is not auto-created')
test:check()

os.exit(0)
