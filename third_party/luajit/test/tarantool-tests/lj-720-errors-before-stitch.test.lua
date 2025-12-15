local tap = require('tap')
local test = tap.test('lj-720-errors-before-stitch'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

local jparse = require('utils').jit.parse

-- `math.modf` recording is NYI.
-- Local `modf` simplifies `jit.dump()` output.
local modf = math.modf

-- XXX: Avoid other traces compilation due to hotcount collisions
-- for predictable results.
jit.off()
jit.flush()

test:plan(2)

-- We only need the abort reason in the test.
jparse.start('t')

jit.opt.start('hotloop=1', 'maxsnap=1')
jit.on()

-- The loop has only two iterations: the first to detect its
-- hotness and the second to record it. The snapshot limit is
-- set to one and is certainly reached.
for _ = 1, 2 do
  -- Forcify stitch.
  modf(1.2)
end

local _, aborted_traces = jparse.finish()

jit.off()

test:ok(true, 'stack is balanced')

-- Tarantool may compile traces on the startup. These traces
-- already exceed the maximum snapshot amount we set after they
-- are compiled. Hence, there is no need to reallocate the
-- snapshot buffer, so the check for the snap size is not
-- triggered.
test:skipcond({
  ['Impossible to predict the number of snapshots for Tarantool'] = _TARANTOOL,
})

assert(aborted_traces and aborted_traces[1], 'aborted trace is persisted')

-- We tried to compile only one trace.
local reason = aborted_traces[1][1].abort_reason

test:like(reason, 'too many snapshots', 'abort reason is correct')

test:done(true)
