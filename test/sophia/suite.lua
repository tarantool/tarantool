#!/usr/bin/env tarantool

local os = require('os')

function sophia_mkdir(dir)
	os.execute("mkdir -p ./sophia/sophia_test")
end

function sophia_rmdir(dir)
	os.execute("rm -rf ./sophia/sophia_test")
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
