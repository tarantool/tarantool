#!/usr/bin/env tarantool

--------------------------------------------------------------------------------
-- All box members must raise an exception on access if box.cfg{} wasn't called
--------------------------------------------------------------------------------

local box = require('box')
local function test()
    return type(box.space)
end

print(pcall(test))
box.cfg{
    logger="tarantool.log",
    slab_alloc_arena=0.1,
}
print(pcall(test))

os.exit(0)
