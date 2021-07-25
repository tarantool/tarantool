#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = 'unix/:./master.sock',
    replication_timeout = 0.1,
    memtx_use_mvcc_engine = true,
})

box.schema.user.grant('guest', 'super')
