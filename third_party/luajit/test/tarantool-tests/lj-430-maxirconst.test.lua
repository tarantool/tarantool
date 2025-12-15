local tap = require('tap')
local test = tap.test('lj-430-maxirconst'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

test:plan(2)

local traceinfo = require('jit.util').traceinfo

-- This function has only 3 IR constant.
local function irconst3()
end

-- This function has 4 IR constants before optimizations.
local function irconst4()
  local _ = 42
end

-- XXX: Avoid any other traces compilation due to hotcount
-- collisions for predictable results.
jit.off()
jit.flush()
jit.opt.start('hotloop=1')

assert(not traceinfo(1), 'no traces compiled after flush')
jit.on()
irconst3()
irconst3()
jit.off()
test:ok(traceinfo(1), 'new trace created')

-- XXX: Trace 1 always has at least 3 IR constants: for nil, false
-- and true. If LUAJIT_ENABLE_CHECKHOOK is set to ON, three more
-- constants are emitted to the trace.
-- Tighten up <maxirconst> and try to record the next trace with
-- one more constant to be emitted.
jit.opt.start(('maxirconst=%d'):format(traceinfo(1).nk))

assert(not traceinfo(2), 'only one trace is created to this moment')
jit.on()
irconst4()
irconst4()
jit.off()
test:ok(not traceinfo(2), 'trace should not appear due to maxirconst limit')

test:done(true)
