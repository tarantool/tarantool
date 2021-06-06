#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))
box.cfg({
    replication = {
        'unix/:./master1.sock',
        'unix/:./master2.sock'
    },
})
