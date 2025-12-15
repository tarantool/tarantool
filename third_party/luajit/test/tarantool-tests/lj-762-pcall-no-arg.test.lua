local tap = require('tap')

-- Test file to check error raising for `pcall()` without
-- arguments. Regardless that the problem is aarch64-specific,
-- it is good to test it for all arches.
-- See also https://github.com/LuaJIT/LuaJIT/issues/762.
local test = tap.test('lj-762-pcall-no-arg')
test:plan(2)

local result, err = pcall(pcall)

test:ok(not result, 'pcall() without args: bad status')
test:like(err, 'value expected', 'pcall() without args: error message')

test:done(true)
