#!/usr/bin/env tarantool

require('console').listen(os.getenv('ADMIN'))
box.cfg({
	listen = 'unix/:./gh-5613-replica1.sock',
	replication = {
		'unix/:./gh-5613-master.sock',
		'unix/:./gh-5613-replica1.sock',
	},
	-- Set to read_only initially so as the bootstrap-master would be
	-- known in advance.
	read_only = true,
})
