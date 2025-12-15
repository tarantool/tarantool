local tap = require('tap')
-- Test file to demonstrate the incorrect JIT assembling of
-- `IR_SLOAD` with typecheck and conversion to integer from
-- number.
-- See also https://github.com/LuaJIT/LuaJIT/issues/917.
local test = tap.test('lj-917-arm64-sload-typecheck-conversion'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

jit.opt.start('hotloop=1')

local results = {}

-- Use the following mathematics on a huge number not fitting into
-- an int to be sure that all 3 control numbers (start, stop,
-- step) of the loop should be non-integers to avoid fallback to
-- the `lj_vmeta_for()` and narrowing in the `lj_meta_for()`
-- (see <src/vm_arm64.dasm> for details).
local NOT_INT = 2 ^ 32

-- The interesting for us SLOAD is the loading of the start index:
-- | 0006 x28   >  int SLOAD  #4    TCI
--
-- Which results in the following mcode before the patch:
-- |  ldr   x28, [x3, #16]
-- |  cmp   x2, x28, lsr #32
-- |  bls   0x62d2fda0        ->0
-- |                              ; here missing the move to d31
-- |  fcvtzs w28, d31
-- |  scvtf d30, w28
-- |  fcmp  d30, d31
-- |  bne   0x62d2fda0        ->0
--
-- Instead of the expected:
-- |  ldr   x28, [x3, #16]
-- |  cmp   x2, x28, lsr #32
-- |  bls   0x7bacfda0        ->0
-- |  fmov  d31, x28
-- |  fcvtzs w28, d31
-- |  scvtf d30, w28
-- |  fcmp  d30, d31
-- |  bne   0x7bacfda0        ->0

-- At this moment d31 contains the value of the `step`, so `step`
-- should be >= `stop` to obtain inconsistency (the too early loop
-- end with the last `i` value equals to `step`).
-- The resulting loop is:
-- | for i = -4, -1, 1 do
for i = -4 + NOT_INT * 0, -1 + NOT_INT * 0, 1 + NOT_INT * 0 do
  results[-i] = true
end

-- Expected {true, true, true, true}, since -4 is a start.
test:samevalues(results, 'correct SLOAD TC assembling')

test:done(true)
