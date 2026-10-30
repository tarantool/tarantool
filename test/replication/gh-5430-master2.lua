#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = 'unix/:./master2.sock',
    replication = {
        'unix/:./master1.sock',
        'unix/:./master2.sock',
    },
    read_only = true,
    replication_synchro_quorum = 2,
    replication_timeout = 0.5,
})
