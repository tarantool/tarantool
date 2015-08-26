#!/usr/bin/env tarantool

require('suite')

if not file_exists('./sophia/lock') then
	sophia_rmdir()
	sophia_mkdir()
end

local sophia = {
	threads = 3
}

box.cfg {
    listen            = os.getenv("LISTEN"),
    slab_alloc_arena  = 0.1,
--    pid_file          = "tarantool.pid",
    rows_per_wal      = 50,
    sophia_dir        = "./sophia/sophia_test",
    sophia            = sophia,
    custom_proc_title = "default"
}

require('console').listen(os.getenv('ADMIN'))
