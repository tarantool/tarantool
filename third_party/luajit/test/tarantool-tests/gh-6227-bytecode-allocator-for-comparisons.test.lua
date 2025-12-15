local tap = require('tap')
local test = tap.test('gh-6227-bytecode-allocator-for-comparisons'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- Test file to demonstrate assertion failure during recording
-- wrong allocated bytecode for comparisons.
-- See also https://github.com/tarantool/tarantool/issues/6227.

-- Need function with RET0 bytecode to avoid reset of
-- the first JIT slot with frame info. Also need no assignments
-- by the caller.
local function empty() end

local uv = 0

-- This function needs to reset register enumerating.
-- `J->maxslot` is initialized with `nargs` (i.e. zero in this
-- case) in `rec_call_setup()`.
local function bump_frame()
  -- First call function with RET0 to set TREF_FRAME in the
  -- last slot.
  empty()
  -- The old bytecode to be recorded looks like the following:
  -- 0000  . FUNCF    4
  -- 0001  . UGET     0   0      ; empty
  -- 0002  . CALL     0   1   1
  -- 0000  . . JFUNCF   1   1
  -- 0001  . . RET0     0   1
  -- 0002  . CALL     0   1   1
  -- 0003  . UGET     0   0      ; empty
  -- 0004  . UGET     3   1      ; uv
  -- 0005  . KSHORT   2   1
  -- 0006  . ISLT     3   2
  -- Test ISGE or ISGT bytecode. These bytecodes swap their
  -- operands (consider ISLT above).
  -- Two calls of `empty()` function in a row is necessary for 2
  -- slot gap in LJ_FR2 mode.
  -- Upvalue loads before KSHORT, so the difference between slot
  -- for upvalue `empty` (function to be called) and slot for
  -- upvalue `uv` is more than 2. Hence, TREF_FRAME slot is not
  -- rewritten by the bytecode after return from `empty()`
  -- function as expected. That leads to recording slots
  -- inconsistency and assertion failure at `rec_check_slots()`.
  empty(1>uv)
end

jit.opt.start('hotloop=1')

for _ = 1, 3 do
  bump_frame()
end

test:ok(true)
test:done(true)
