#!/usr/bin/env tarantool
os = require('os')

local sophia_options = {
	memory_limit   = 0,
	threads        = 3, -- test case
	node_size      = 134217728,
	node_page_size = 131072,
	node_branch_wm = 10485760,
    node_merge_wm  = 1
}

box.cfg{
    listen           = os.getenv("LISTEN"),
    slab_alloc_arena = 0.1,
    pid_file         = "tarantool.pid",
    rows_per_wal     = 50,
    sophia_dir       = "sophia_test",
    sophia_options   = sophia_options
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
