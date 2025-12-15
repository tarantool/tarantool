local tap = require('tap')

-- Test file to demonstrate LuaJIT `tonumber('-0')` incorrect
-- behaviour.
-- See also https://github.com/LuaJIT/LuaJIT/issues/528,
-- https://github.com/LuaJIT/LuaJIT/pull/787.
local test = tap.test('lj-528-tonumber-0')
test:plan(1)

-- As numbers -0 equals to 0, so convert it back to string.
test:ok(tostring(tonumber('-0')) == '-0', 'correct "-0" string parsing')

test:done(true)
