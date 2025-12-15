local ffi = require('ffi')
local table_new = require('table.new')

-- Test file to demonstrate LuaJIT incorrect behaviour for
-- recording the FFI call to the vararg function. See also:
-- https://github.com/LuaJIT/LuaJIT/issues/1244.
local tap = require('tap')
local test = tap.test('lj-1244-missing-phi-carg'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

-- Loop unrolls into 2 iterations. Thus means that the loop is
-- executed on trace on the 5th iteration (instead of the usual
-- 4th). Run it even number of iterations to test both, so last is
-- 6th.
local NTESTS = 6

test:plan(NTESTS)

-- XXX: Hack with function's prototypes to avoid creation of
-- custom functions to be loaded via FFI (vararg part will be just
-- ignored).
ffi.cdef[[
  double sin(double, ...);
  double cos(double, ...);
]]

local EXPECTED = {[0] = ffi.C.sin(0), ffi.C.cos(0)}

-- Array of 2 functions.
local fns = ffi.new('double (*[2])(double, ...)')
fns[0] = ffi.C.cos
fns[1] = ffi.C.sin

-- Avoid reallocating the table on the trace.
local result = table_new(8, 0)

jit.opt.start('hotloop=1')

local fn = fns[0]
-- The first result is `cos()`.
for i = 1, NTESTS do
  result[i] = fn(0)
  fn = fns[i % 2]
  -- The call persists in the invariant part of the loop as well.
  -- Hence, XLOAD (part of the IR_CARG -- function to be called)
  -- should be marked as PHI, but it isn't due to CSE.
  fn(0)
end

for i = 1, NTESTS do
  test:is(result[i], EXPECTED[i % 2],
          ('correct result on iteration %d'):format(i))
end

test:done(true)
