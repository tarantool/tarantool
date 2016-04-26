#!/usr/bin/env tarantool

require('suite')

if not file_exists('./phia/lock') then
	phia_rmdir()
	phia_mkdir()
end

local phia = {
	threads = 3
}

box.cfg {
    listen            = os.getenv("LISTEN"),
    slab_alloc_arena  = 0.1,
--    pid_file          = "tarantool.pid",
    rows_per_wal      = 50,
    phia_dir        = "./phia/phia_test",
    phia            = phia
}

require('console').listen(os.getenv('ADMIN'))
