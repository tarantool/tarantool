local tap = require('tap')
local test = tap.test('fix-argv2ctype-cts-L-init'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

-- Loading of 'tap' module initialize `cts->L` during parsing.
-- Run standalone script for testing.
local script = require('utils').exec.makecmd(arg)

test:plan(1)

local output = script()
test:is(output, 'OK', 'correct recording with uninitialized cts->L')

test:done(true)
