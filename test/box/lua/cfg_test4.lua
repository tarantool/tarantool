#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    slab_alloc_factor = 3.14,
    phia = {memory_limit = 1024 * 1024},
}

require('console').listen(os.getenv('ADMIN'))
box.schema.user.grant('guest', 'read,write,execute', 'universe')
