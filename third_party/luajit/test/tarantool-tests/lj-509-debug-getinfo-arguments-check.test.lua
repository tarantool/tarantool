local tap = require('tap')

-- Test file to demonstrate crash in the `debug.getinfo()` call.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/509.
local test = tap.test('lj-509-debug-getinfo-arguments-check.test.lua')
test:plan(2)

-- '>' expects to have an extra argument on the stack.
local res, err = pcall(debug.getinfo, 1, '>S')
test:ok(not res, 'check result of the call with invalid arguments')
test:like(err, 'bad argument', 'check the error message')

test:done(true)
