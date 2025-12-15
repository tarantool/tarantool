local tap = require('tap')
-- The test reproduces the problem only for GC64 mode with enabled
-- JIT.
local test = tap.test('lj-962-stack-overflow-report')
test:plan(1)

local LUABIN = require('utils').exec.luabin(arg)
local SCRIPT = arg[0]:gsub('%.test%.lua$', '/script.lua')
local output = io.popen(LUABIN .. ' 2>&1 ' .. SCRIPT, 'r'):read('*all')
test:like(output, 'stack overflow', 'stack overflow reported correctly')
test:done(true)
