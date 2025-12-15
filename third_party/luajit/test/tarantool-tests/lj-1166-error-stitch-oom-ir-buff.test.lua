local tap = require('tap')

-- Test file to demonstrate unbalanced Lua stack after instruction
-- recording due to throwing an error at recording of a stitched
-- function.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1166.

local test = tap.test('lj-1166-error-stitch-oom-ir-buff'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

local jparse = require('utils').jit.parse
local allocinject = require('allocinject')

local IS_DUALNUM = tostring(tonumber('-0')) ~= tostring(-0)

-- XXX: Avoid other traces compilation due to hotcount collisions
-- for predictable results.
jit.off()
jit.flush()

test:plan(2)

-- Generate the following Lua chunk:
-- local s1
-- ...
-- local sN
-- for i = 1, 2 do
--   s1 = i + 1
--   ...
--   sN = i + N
--   math.modf(1)
-- end
local function create_chunk(n_slots)
  local chunk = ''
  for i = 1, n_slots do
    chunk = chunk .. ('local s%d\n'):format(i)
  end
  chunk = chunk .. 'for i = 1, 2 do\n'
  -- Generate additional IR instructions.
  for i = 1, n_slots do
    chunk = chunk .. ('  s%d = i + %d\n'):format(i, i)
  end
  -- `math.modf()` recording is NYI.
  chunk = chunk .. '  math.modf(1)\n'
  chunk = chunk .. 'end\n'
  return chunk
end

-- XXX: amount of slots is empirical.
local tracef = assert(loadstring(create_chunk(175)))

-- We only need the abort reason in the test.
jparse.start('t')

jit.on()
jit.opt.start('hotloop=1', '-loop', '-fold')

allocinject.enable_null_doubling_realloc()

tracef()

allocinject.disable()

local _, aborted_traces = jparse.finish()

jit.off()

test:ok(true, 'stack is balanced')

-- Tarantool may compile traces on the startup. These traces
-- already exceed the maximum IR amount before the trace in this
-- test is compiled. Hence, there is no need to reallocate the IR
-- buffer, so the check for the IR size is not triggered.
test:skipcond({
  ['Impossible to predict the number of IRs for Tarantool'] = _TARANTOOL,
  -- The amount of IR for traces is different for non x86/x64
  -- arches and DUALNUM mode.
  ['Disabled for non-x86_64 arches'] = jit.arch ~= 'x64' and jit.arch ~= 'x86',
  ['Disabled for DUALNUM mode'] = IS_DUALNUM,
})

assert(aborted_traces and aborted_traces[1], 'aborted trace is persisted')

-- We tried to compile only one trace.
local reason = aborted_traces[1][1].abort_reason

test:like(reason, 'error thrown or hook called during recording',
          'abort reason is correct')

test:done(true)
