#!/usr/bin/env tarantool

local os = require('os')

local ffi = require('ffi')
ffi.cdef[[
	int sophia_schedule(void);
]]

function sophia_schedule()
	ffi.C.sophia_schedule()
end

function sophia_dir()
	local i = 0
	local list = {}
	for file in io.popen("ls -1 sophia_test"):lines() do
		i = i + 1
		list[i] = file
	end
	return {i, t}
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
