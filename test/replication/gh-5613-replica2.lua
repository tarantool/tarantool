#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))
box.cfg({
	listen = 'unix/:./gh-5613-replica2.sock',
	replication = {
		'unix/:./gh-5613-master.sock',
		'unix/:./gh-5613-replica1.sock',
		'unix/:./gh-5613-replica2.sock',
	},
})
