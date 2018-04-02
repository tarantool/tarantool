#!/usr/bin/env tarantool

box.cfg({
    listen              = os.getenv("LISTEN"),
    replication         = os.getenv("MASTER"),
    memtx_memory        = 107374182,
    replication_connect_quorum = 0,
    replication_connect_timeout = 0.1,
})

require('console').listen(os.getenv('ADMIN'))
