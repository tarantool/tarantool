#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('cfg')
test:plan(1)

local function file_exists(name)
	local f = io.open(name,"r")
	if f ~= nil then
		io.close(f)
		return true
	else
		return false
	end
end

os.execute("rm -rf sophia")
local box = require('box')
box.cfg{
    logger="tarantool.log",
    slab_alloc_arena=0.1,
    wal_mode = ""
}
test:is(file_exists("sophia"), false, 'sophia_dir is not auto-created')
test:check()

os.exit(0)
