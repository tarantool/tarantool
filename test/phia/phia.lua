#!/usr/bin/env tarantool

require('suite')

if not file_exists('./phia/lock') then
	phia_rmdir()
	phia_mkdir()
end

box.cfg {
    listen            = os.getenv("LISTEN"),
    slab_alloc_arena  = 0.1,
    rows_per_wal      = 1000000,
    phia_dir        = "./phia/phia_test",
    phia = {
        threads = 3;
        memory_limit = 0.05;
    }
}

require('console').listen(os.getenv('ADMIN'))
