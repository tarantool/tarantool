local tap = require('tap')

-- Test file to demonstrate unbalanced Lua stack after instruction
-- recording due to throwing an error at recording of a stitched
-- function.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1166.

local test = tap.test('lj-1166-error-stitch-oom-snap-buff'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

local jparse = require('utils').jit.parse
local allocinject = require('allocinject')

-- XXX: Avoid other traces compilation due to hotcount collisions
-- for predictable results.
jit.off()
jit.flush()

test:plan(2)

-- Generate the following Lua chunk:
-- for i = 1, 2 do
--   if i < 1 then end
--   ...
--   if i < N then end
--   math.modf(1)
-- end
local function create_chunk(n_conds)
  local chunk = ''
  chunk = chunk .. 'for i = 1, 2 do\n'
  -- Each condition adds additional snapshot.
  for i = 1, n_conds do
    chunk = chunk .. ('  if i < %d then end\n'):format(i + n_conds)
  end
  -- `math.modf()` recording is NYI.
  chunk = chunk .. '  math.modf(1)\n'
  chunk = chunk .. 'end\n'
  return chunk
end

jit.on()
-- XXX: Need to compile the cycle in the `create_chunk()` to
-- preallocate the snapshot buffer.
jit.opt.start('hotloop=1', '-loop', '-fold')

-- XXX: Amount of slots is empirical.
local tracef = assert(loadstring(create_chunk(6)))

-- XXX: Remove previous trace.
jit.off()
jit.flush()

-- We only need the abort reason in the test.
jparse.start('t')

-- XXX: Update hotcounts to avoid hash collisions.
jit.opt.start('hotloop=1')
jit.on()

allocinject.enable_null_doubling_realloc()

tracef()

allocinject.disable()

local _, aborted_traces = jparse.finish()

jit.off()

test:ok(true, 'stack is balanced')

-- Tarantool may compile traces on the startup. These traces
-- already exceed the maximum snapshot amount before the trace in
-- this test is compiled. Hence, there is no need to reallocate
-- the snapshot buffer, so the check for the snap size is not
-- triggered.
test:skipcond({
  ['Impossible to predict the number of snapshots for Tarantool'] = _TARANTOOL,
})

assert(aborted_traces and aborted_traces[1], 'aborted trace is persisted')

-- We tried to compile only one trace.
local reason = aborted_traces[1][1].abort_reason

test:like(reason, 'error thrown or hook called during recording',
          'abort reason is correct')

test:done(true)
