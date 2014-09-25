#!/usr/bin/env tarantool
os = require('os')

box.cfg{
    listen              = os.getenv("LISTEN"),
    slab_alloc_arena    = 0.1,
    pid_file            = "tarantool.pid",
    rows_per_wal        = 50,
    sophia_dir          = "sophia_test"
}

require('console').listen(os.getenv('ADMIN'))

function sophia_printdir()
	f = io.popen("ls -1 sophia_test")
	ls = f:read("*all")
	unused = f:close()
	return ls
end

function sophia_rmdir(dir)
	os.execute("rm -rf sophia_test")
end
