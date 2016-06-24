#!/usr/bin/env tarantool

local os = require('os')

function vinyl_mkdir(dir)
	os.execute("mkdir -p ./vinyl/vinyl_test")
end

function vinyl_rmdir(dir)
	os.execute("rm -rf ./vinyl/vinyl_test")
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
