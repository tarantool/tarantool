local tap = require('tap')

-- Test file to demonstrate incorrect FOLD optimization for IR
-- with REF_BASE operand across IR RETF.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/784.

local test = tap.test('lj-784-cse-ref-base-over-retf'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- The RETF IR has a side effect: it shifts base when returning to
-- a lower frame, i.e., it affects `REF_BASE` IR (0000) (thus, we
-- can say that this IR is violating SSA form).
-- So any optimization of IRs with `REF_BASE` as an operand across
-- RETF IR may lead to incorrect optimizations.
-- In this test, SUB uref REF_BASE IR was eliminated, so instead
-- the following trace:
--
-- 0004    p32 SUB    0003  0000
-- 0005 >  p32 UGT    0004  +32
-- ...
-- 0009 >  p32 RETF   proto: 0x407dc118  [0x407dc194]
-- ...
-- 0012    p32 SUB    0003  0000
-- 0013 >  p32 UGT    0012  +72
--
-- We got the following:
--
-- 0004    p32 SUB    0003  0000
-- 0005 >  p32 UGT    0004  +32
-- ...
-- 0009 >  p32 RETF   proto: 0x41ffe0c0  [0x41ffe13c]
-- ...
-- 0012 >  p32 UGT    0004  +72
--
-- As you can see, the 0012 SUB IR is eliminated because it is the
-- same as the 0004 IR. This leads to incorrect assertion guards
-- in the resulted IR 0012 below.

local MAGIC = 42
-- XXX: simplify `jit.dump()` output.
local fmod =  math.fmod

local function exit_with_retf(closure)
  -- Forcify stitch. Any NYI is OK here.
  fmod(1, 1)
  -- Call the closure so that we have emitted `uref - REF_BASE`.
  closure(0)
  -- Exit with `IR_RETF`. This will change `REF_BASE`.
end

local function sub_uref_base(closure)
  local open_upvalue
  if closure == nil then
    closure = function(val)
      local old = open_upvalue
      open_upvalue = val
      return old
    end
    -- First, create an additional frame, so we got the trace,
    -- where the open upvalue reference is always < `REF_BASE`.
    sub_uref_base(closure)
  end
  for _ = 1, 4 do
    -- `closure` function is inherited from the previous frame.
    exit_with_retf(closure)
    open_upvalue = MAGIC
    -- The open upvalue guard will use CSE over `IR_RETF` for
    -- `uref - REF_BASE`. `IR_RETF` changed the value of
    -- `REF_BASE`.
    -- Thus, the guards afterwards take the wrong IR as the first
    -- operand, so they are not failed, and the wrong value is
    -- returned from the trace.
    open_upvalue = closure(0)
  end
  return open_upvalue
end

jit.opt.start('hotloop=1')

local res = sub_uref_base()
test:is(res, MAGIC, 'no SUB uref REF_BASE CSE across RETF')

test:done(true)
