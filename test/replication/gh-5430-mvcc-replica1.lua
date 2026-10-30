#!/usr/bin/env tarantool
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = os.getenv("LISTEN"),
    replication = 'unix/:./master.sock',
    replication_timeout = 0.1,
    read_only = true,
    replication_anon = true,
})
