#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    slab_alloc_arena = 0.2,
    sophia = {threads = 10},
}

require('console').listen(os.getenv('ADMIN'))
