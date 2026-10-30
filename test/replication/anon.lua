#!/usr/bin/env tarantool

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = os.getenv("MASTER"),
    memtx_memory        = 107374182,
    replication_timeout = 0.1,
    replication_connect_timeout = 0.5,
    read_only=true,
    replication_anon=true,
})

require('console').listen(os.getenv('ADMIN'))
