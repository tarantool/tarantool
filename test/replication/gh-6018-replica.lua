#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))

box.cfg({
    listen = 'unix/:./gh-6018-replica.sock',
    replication = {
	'unix/:./gh-6018-master.sock',
	'unix/:./gh-6018-replica.sock',
    },
    election_mode = 'voter',
    -- Smaller than master UUID.
    instance_uuid = 'cbf06940-0790-498b-948d-042b62cf3d28',
    replication_timeout = 0.1,
})
