#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))
box.cfg({
    listen = 'unix/:./master2.sock',
    replication = {
        'unix/:./master1.sock',
        'unix/:./master2.sock'
    },
    election_mode = 'voter',
    election_timeout = 0.1,
    instance_uuid = '20f9828d-b5d5-46a9-b698-ddac7cce5e27',
})
