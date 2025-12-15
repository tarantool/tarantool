local tap = require('tap')

-- Test file to demonstrate incorrect behaviour when LuaJIT
-- flushes the trace twice when another trace for the same
-- starting bytecode was recorded.
-- See also https://github.com/LuaJIT/LuaJIT/issues/1345.
local test = tap.test('lj-1345-flushing-trace-twice'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- Reset JIT.
jit.flush()
collectgarbage()

jit.opt.start('hotloop=1')

for _ = 1, 3  do
  -- Nothing to flush on the first iteration. On the second
  -- iteration, flushing the trace for the loop below (from the
  -- first iteration). On the third iteration, another trace (from
  -- the second iteration) is recorded for that loop.
  -- This leads to the assertion failure before this patch.
  jit.flush(1)
  -- Record the loop with a trace.
  for _ = 1, 4 do end
end

test:ok(true, 'no assertion failure during trace flushing')
test:done(true)
