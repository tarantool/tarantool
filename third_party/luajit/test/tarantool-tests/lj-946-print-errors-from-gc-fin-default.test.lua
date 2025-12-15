local tap = require('tap')
local test = tap.test('lj-flush-on-trace'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local script = require('utils').exec.makecmd(arg, { redirect = '2>&1' })
local output = script()
test:like(output, 'ERROR in finalizer:', 'error handler called')
test:done(true)
