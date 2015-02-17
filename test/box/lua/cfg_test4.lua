#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    slab_alloc_factor = 3.14,
    sophia = {page_size = 1234},
}

require('console').listen(os.getenv('ADMIN'))
