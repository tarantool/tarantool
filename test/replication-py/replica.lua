#!/usr/bin/env tarantool
require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = os.getenv("MASTER"),
    memtx_memory        = 107374182,
    replication_timeout = 0.1
})
