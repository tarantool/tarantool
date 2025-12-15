local tap = require('tap')

-- Test file to demonstrate the JIT misbehaviour, when the side
-- trace is compiled after `jit.off()`.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1134.

local test = tap.test('lj-1134-hotside-jit-off'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

-- `traceinfo()` takes the trace number as an argument.
local traceinfo = require('jit.util').traceinfo

test:plan(1)

local take_side
local function trace()
  -- luacheck: ignore
  -- Branch for the side exit.
  if take_side then end
end

-- Flush all possible traces.
jit.flush()

jit.opt.start('hotloop=1', 'hotexit=1')

trace()
trace()

assert(traceinfo(1), 'root trace not compiled')

-- Force trace exit.
take_side = true

jit.off()

-- Attempt to compile a side trace.
trace()
trace()

test:ok(not traceinfo(2), 'no side trace compiled')

test:done(true)
