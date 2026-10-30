#!/usr/bin/env tarantool

box.cfg {
    listen = os.getenv("LISTEN"),
    force_recovery = true
}

require('console').listen(os.getenv('ADMIN'))
