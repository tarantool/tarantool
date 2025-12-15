local tap = require('tap')
local test = tap.test('fix-emit-rma'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Test requires GC64 mode enabled'] = not require('ffi').abi('gc64'),
})

-- Need to test 2 cases of `emit_rma()` particularly on x64:
--   * `IR_LDEXP` with `fld` instruction for loading constant
--      number `TValue` by address.
--   * `IR_OBAR` with the corresponding `test` instruction on
--     `marked` field of `GCobj`.
-- Also, test correctness.
test:plan(4)

local ffi = require('ffi')

collectgarbage()
-- Chomp memory in currently allocated GC space.
collectgarbage('stop')

for _ = 1, 8 do
  ffi.new('char[?]', 256 * 1024 * 1024)
end

jit.opt.start('hotloop=1')

-- Test `IR_LDEXP`.

-- Reproducer here is a little tricky.
-- We need to generate a bunch of traces as far we reference an
-- IR field (`TValue`) address in `emit_rma()`. The amount of
-- traces is empirical. Usually, assert fails on ~33d iteration,
-- so let's use 100 just to be sure.
local test_marker
for _ = 1, 100 do
  test_marker = loadstring([[
    local test_marker
    for i = 1, 4 do
      -- Avoid fold optimization, use `i` as the second argument.
      -- Need some constant differs from 1 or 0 as the first
      -- argument.
      test_marker = math.ldexp(1.2, i)
    end
    return test_marker
  ]])()
end

-- If we are here, it means no assertion failed during emitting.
test:ok(true, 'IR_LDEXP emit_rma')
test:ok(test_marker == math.ldexp(1.2, 4), 'IR_LDEXP emit_rma check result')

-- Test `IR_OBAR`.

-- First, create a closed upvalue.
do
  local uv -- luacheck: no unused
  -- `IR_OBAR` is used for object write barrier on upvalues.
  _G.change_uv = function(newv)
    uv = newv
  end
end

-- We need a constant value on trace to be referenced far enough
-- from dispatch table. So we need to create a new function
-- prototype with a constant string.
-- This string should be long enough to be allocated with direct
-- alloc (not fitting in free chunks) far away from dispatch.
-- See <src/lj_alloc.c> for details.
local DEFAULT_MMAP_THRESHOLD = 128 * 1024
local str = string.rep('x', DEFAULT_MMAP_THRESHOLD)
local func_with_trace = loadstring([[
  for _ = 1, 4 do
    change_uv(']] .. str .. [[')
  end
]])
func_with_trace()

-- If we are here, it means no assertion failed during emitting.
test:ok(true, 'IR_OBAR emit_rma')

-- Now check the correctness.

-- Set GC state to GCpause.
collectgarbage()

-- We want to wait for the situation, when upvalue is black,
-- the string is gray. Both conditions are satisfied, when the
-- corresponding `change_uv()` function is marked, for example.
-- We don't know on what exact step our upvalue is marked as black
-- and execution of trace become dangerous, so just check it at
-- each step.
-- Don't need to do the full GC cycle step by step.
local old_steps_atomic = misc.getmetrics().gc_steps_atomic
while (misc.getmetrics().gc_steps_atomic == old_steps_atomic) do
  collectgarbage('step')
  func_with_trace()
end

-- If we are here, it means no assertion failed during
-- `gc_mark()`, - due to wrong call to `lj_gc_barrieruv()` on
-- trace.
test:ok(true, 'IR_OBAR emit_rma check correctness')

test:done(true)
