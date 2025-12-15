local tap = require('tap')

-- Test file to check correctness of external unwinding
-- in LuaJIT.
-- See also https://github.com/LuaJIT/LuaJIT/issues/698,
-- https://github.com/LuaJIT/LuaJIT/pull/757.
local test = tap.test('gh-6096-external-unwinding-on-arm64')
test:plan(1)

local res = pcall(require, 'not-existing-module')
test:ok(res == false, 'successful unwinding in pcall')

test:done(true)
