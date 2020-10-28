#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = os.getenv("LISTEN"),
    force_recovery = true,
    read_only = false,
})
