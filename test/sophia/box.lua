#!/usr/bin/env tarantool
os = require('os')

function sophia_printdir()
	f = io.popen("ls -1 sophia_test")
	ls = f:read("*all")
	unused = f:close()
	return ls
end

function sophia_mkdir(dir)
	os.execute("mkdir sophia_test")
end

function sophia_rmdir(dir)
	os.execute("rm -rf sophia_test")
end

function file_exists(name)
	local f = io.open(name,"r")
	if f ~= nil then
		io.close(f)
		return true
	else
		return false
	end
end

if not file_exists("lock") then
	sophia_rmdir()
	sophia_mkdir()
end

local sophia = {
	threads = 3 -- test case
}

box.cfg {
    listen           = os.getenv("LISTEN"),
    slab_alloc_arena = 0.1,
    pid_file         = "tarantool.pid",
    rows_per_wal     = 50,
    sophia_dir       = "./sophia_test",
    sophia           = sophia
}

require('console').listen(os.getenv('ADMIN'))
