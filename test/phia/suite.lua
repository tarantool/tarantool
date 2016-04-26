#!/usr/bin/env tarantool

local os = require('os')

function phia_mkdir(dir)
	os.execute("mkdir -p ./phia/phia_test")
end

function phia_rmdir(dir)
	os.execute("rm -rf ./phia/phia_test")
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
