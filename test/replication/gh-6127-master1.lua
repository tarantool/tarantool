#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))
box.cfg({
    listen = 'unix/:./master1.sock',
    replication = {
        'unix/:./master1.sock',
        'unix/:./master2.sock'
    },
    election_mode = 'candidate',
    election_timeout = 0.1,
    instance_uuid = '10f9828d-b5d5-46a9-b698-ddac7cce5e27',
})
box.ctl.wait_rw()
box.schema.user.grant('guest', 'super')
