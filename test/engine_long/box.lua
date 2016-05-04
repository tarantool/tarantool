#!/usr/bin/env tarantool

require('suite')

os.execute("rm -rf phia_test")
os.execute("mkdir -p phia_test")

local phia = {
	threads = 5
}

box.cfg {
    listen            = os.getenv("LISTEN"),
    slab_alloc_arena  = 0.1,
    pid_file          = "tarantool.pid",
    rows_per_wal      = 500000,
    phia_dir        = "./phia_test",
    phia            = phia,
}

require('console').listen(os.getenv('ADMIN'))
