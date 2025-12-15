local tap = require('tap')
-- Test to demonstrate the incorrect LuaJIT behavior when exiting
-- from a snapshot for stitched trace.
local test = tap.test('lj-913-stackoverflow-stitched-trace'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(3)

-- Recursion to cause stack overflow.
local function callee()
  -- `math.fmod()` is NYI, so trace will be stitched here.
  math.fmod(42, 42)
  callee()
end

local st, err = pcall(callee)

test:ok(true, 'assertion is not triggered')
test:ok(not st, 'error happened')
test:like(err, 'stack overflow', 'stack overflow happened')

test:done(true)
