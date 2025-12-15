local tap = require('tap')
-- Test file to demonstrate LuaJIT misbehaviour in use-def
-- snapshot analysis for BC_VARG.
-- See also https://github.com/LuaJIT/LuaJIT/issues/704.
local test = tap.test('lj-704-bc-varg-use-def'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

-- XXX: we don't really need to store these builtins, but this
-- reduces `jitdump()` output for reader significantly.
local fmod = math.fmod
local pcall = pcall

-- Use the 2 values for `fmod()` to produce non-zero value for
-- the call on trace (the last one call). No special meaning.
local ARG_ON_RECORDING = 6
local ON_TRACE_VALUE = ARG_ON_RECORDING + 1

-- The `jitdump()` output was like the following before the patch:
-- 0003 >  num SLOAD  #1    T
-- ....        SNAP   #1   [`wrap()`|---- pcall|`varg()`|----]
-- 0004 }  tab TNEW   #3    #0
-- 0005 >  num SLOAD  #4    T
-- 0006    p32 FLOAD  0004  tab.array
-- 0007    p32 AREF   0006  +1
-- 0008 }  num ASTORE 0007  0005
-- ....        SNAP   #2   [`wrap()`|---- pcall|math.fmod|+6 0005]
--
-- The first snapshot misses the 0003 IR in the last slot to be
-- used in the `fmod()` later, so it leads to the additional
-- 0005 SLOAD #4, and storing it in the second snapshot.
--
-- The correct snapshot content after the patch is the following:
-- ....        SNAP   #1   [`wrap()`|---- pcall|`varg()`|0003]
-- ....
-- ....        SNAP   #2   [`wrap()`|---- pcall|math.fmod|+6 0003]
local function varg(...)
  -- Generate snapshot after `pcall()` with missing slot.
  -- The snapshot is generated before each TNEW after the commit
  -- 7505e78bd6c24cac6e93f5163675021734801b65 ("Handle on-trace
  -- OOM errors from helper functions.")
  local slot = ({...})[1]
  -- Forcify stitch and usage of vararg slot. Any NYI is OK here.
  return fmod(ARG_ON_RECORDING, slot)
end

jit.opt.start('hotloop=1')

local _, result
local function wrap(arg)
  -- `pcall()` is needed to emit snapshot to handle on-trace
  -- errors.
  _, result = pcall(varg, arg)
end
-- Record trace with the 0 result.
wrap(ARG_ON_RECORDING)
wrap(ARG_ON_RECORDING)
-- Record trace with the non-zero result.
wrap(ON_TRACE_VALUE)

test:ok(result ~= 0, 'use-def analysis for BC_VARG')

-- Now check the same case, but with BC_JMP before the BC_VARG,
-- so use-def analysis will take early return case for BCMlit.
-- See `snap_usedef()` in <src/lj_snap.c> for details.
-- The test checks that slots greater than `numparams` are not
-- purged.
local function varg_ret_bc(...)
  -- XXX: This branch contains BC_JMP. See the comment above.
  -- luacheck: ignore
  if false then else end
  local slot = ({...})[1]
  -- Forcify stitch and usage of vararg slot. Any NYI is OK here.
  return fmod(ARG_ON_RECORDING, slot)
end

-- XXX: Duplicate wrapper code to avoid recording of `wrap()`
-- instead of the function to test.
local function wrap_ret_bc(arg)
  _, result = pcall(varg_ret_bc, arg)
end

-- Record trace with the 0 result.
wrap_ret_bc(ARG_ON_RECORDING)
wrap_ret_bc(ARG_ON_RECORDING)
-- Record trace with the non-zero result.
wrap_ret_bc(ON_TRACE_VALUE)

test:ok(result ~= 0, 'use-def analysis for FUNCV with jump before BC_VARG')

test:done(true)
