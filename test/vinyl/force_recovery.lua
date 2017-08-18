#!/usr/bin/env tarantool

box.cfg {
    listen = os.getenv("LISTEN"),
    vinyl_memory = 128 * 1024 * 1024,
    force_recovery = true,
}

require('console').listen(os.getenv('ADMIN'))
