#!/usr/bin/env tarantool

box.cfg({
    listen = os.getenv("LISTEN"),
    replication = os.getenv("MASTER"),
    read_only = true,
    memtx_use_mvcc_engine = true,
})

require('console').listen(os.getenv('ADMIN'))
