local tap = require('tap')

-- Test file to demonstrate LuaJIT incorrect restoring of sunk
-- tables with double usage of IR_NEWREF.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1128.

local test = tap.test('lj-1128-double-ir-newref-on-restore-sunk'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(3)

local take_side

local function trace_base(num)
  local tab = {}
  tab.key = false
  -- This check can't be folded since `num` can be NaN.
  tab.key = num == num
  -- luacheck: ignore
  -- This side trace emits the following IRs:
  -- 0001    tab TNEW   #0    #0
  -- 0002    p64 NEWREF 0001  "key"
  -- 0003    fal HSTORE 0002  false
  -- 0004    p64 NEWREF 0001  "key"
  -- 0005    tru HSTORE 0004  true
  -- As we can see, `NEWREF` is emitted twice. This is a violation
  -- of its semantics, so the second store isn't noticeable.
  if take_side then end
  return tab.key
end

-- Uncompiled function to end up side trace here.
local function trace_base_wp(num)
  return trace_base(num)
end
jit.off(trace_base_wp)

-- Same function as above, but with two IRs NEWREF emitted.
-- The last NEWREF references another key.
local function trace_2newref(num)
  local tab = {}
  tab.key1 = false
  -- This + op can't be folded since `num` can be -0.
  tab.key1 = num + 0
  tab.key2 = false
  -- This check can't be folded since `num` can be NaN.
  tab.key2 = num == num
  -- luacheck: ignore
  if take_side then end
  return tab.key1, tab.key2
end

-- Uncompiled function to end up side trace here.
local function trace_2newref_wp(num)
  return trace_2newref(num)
end
jit.off(trace_2newref_wp)

jit.opt.start('hotloop=1', 'hotexit=1', 'tryside=1')

-- Compile parent traces.
trace_base_wp(0)
trace_base_wp(0)
trace_2newref_wp(0)
trace_2newref_wp(0)

-- Compile side traces.
take_side = true
trace_base_wp(0)
trace_base_wp(0)
trace_2newref_wp(0)
trace_2newref_wp(0)

test:is(trace_base(0), true, 'sunk value restored correctly')

local arg = 0
local r1, r2 = trace_2newref(arg)
-- These tests didn't fail before the patch.
-- They check the patch's correctness.
test:is(r1, arg, 'sunk value restored correctly with 2 keys, first key')
test:is(r2, true, 'sunk value restored correctly with 2 keys, second key')

test:done(true)
