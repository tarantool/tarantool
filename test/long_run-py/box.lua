#!/usr/bin/env tarantool

require('suite')

os.execute("rm -rf sophia_test")
os.execute("mkdir -p sophia_test")

local sophia = {
	threads = 5
}

box.cfg {
    listen            = os.getenv("LISTEN"),
    slab_alloc_arena  = 0.1,
    pid_file          = "tarantool.pid",
    rows_per_wal      = 500000,
    sophia_dir        = "./sophia_test",
    sophia            = sophia,
}

require('console').listen(os.getenv('ADMIN'))
