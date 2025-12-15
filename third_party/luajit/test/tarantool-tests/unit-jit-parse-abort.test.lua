local tap = require('tap')
local test = tap.test('unit-jit-parse-abort'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

local jparse = require('utils').jit.parse

-- XXX: Avoid other traces compilation due to hotcount collisions
-- for predictable results.
jit.off()
jit.flush()

test:plan(1)

jit.on()
-- We only need the abort reason in the test.
jparse.start('t')

-- XXX: A trace always has at least 3 IR constants: for `nil`,
-- `false`, and `true`. Always fails to record with the set
-- `maxirconst` limit.
jit.opt.start('hotloop=1', 'maxirconst=1')

for _ = 1, 3 do end

local _, aborted_traces = jparse.finish()

jit.off()

assert(aborted_traces and aborted_traces[1], 'aborted trace is persisted')

-- We tried to compile only one trace.
local reason = aborted_traces[1][1].abort_reason

test:like(reason, 'trace too long', 'abort reason is correct')

test:done(true)
