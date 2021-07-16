#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = 'unix/:./gh-6018-master.sock',
    replication = {
	'unix/:./gh-6018-master.sock',
	'unix/:./gh-6018-replica.sock',
    },
    election_mode = arg[1],
    instance_uuid = 'cbf06940-0790-498b-948d-042b62cf3d29',
    replication_timeout = 0.1,
})

box.ctl.wait_rw()
box.schema.user.grant('guest', 'super')
