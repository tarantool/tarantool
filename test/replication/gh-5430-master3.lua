#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = 'unix/:./master3.sock',
    replication = {
        'unix/:./master1.sock',
    },
    replication_synchro_quorum = 3,
    replication_timeout = 0.5,
})
