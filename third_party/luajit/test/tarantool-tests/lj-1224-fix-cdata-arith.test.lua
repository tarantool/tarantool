local tap = require('tap')

-- Test file to demonstrate LuaJIT's incorrect recording of cdata
-- arithmetic, which raises the error.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1224.

local test = tap.test('lj-1224-fix-cdata-arith'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

-- The loading of the 'tap' module initializes `cts->L` during
-- parsing. Run standalone script for testing.
local script = require('utils').exec.makecmd(arg)

test:plan(1)

local output = script()
test:is(output, 'OK', 'correct recording with uninitialized cts->L')

test:done(true)
