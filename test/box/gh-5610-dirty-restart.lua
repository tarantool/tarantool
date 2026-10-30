#!/usr/bin/env tarantool

box.cfg{
    listen = os.getenv("LISTEN"),
    replication_synchro_quorum = 2,
    replication_synchro_timeout = 100,
    memtx_use_mvcc_engine = true,
}

require('console').listen(os.getenv('ADMIN'))
