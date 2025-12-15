local tap = require('tap')

-- The test file to demonstrate the LuaJIT misbehaviour during
-- stack overflow on snapshot restoration for the last bytecode in
-- the prototype.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1359.

local test = tap.test('lj-1359-bad-pc-on-snap-restore-stackov'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

jit.opt.start('hotloop=1')

-- When restoring a snapshot because of exiting by the check of
-- the stack, the snapshot from the parent trace is used. For the
-- correct error message, the snapshot map uses the next bytecode
-- after the stored one. In case, when the PC in the snapshot is
-- BC_RET* (the last bytecode in the prototype), there is no next
-- value to be referenced, so this approach leads to the assertion
-- failure in lj_debug_framepc().

local counter = 0
-- The second trace started from the side exit by the counter.
-- It ends by entering the first trace.
local function func_side_trace()
  if counter > 5 then
    -- This RET0 BC is the first recorded for the second trace.
    -- The stack check for the child trace uses the first snapshot
    -- from the parent trace.
    return
  end
  counter = counter + 1;
end

-- The trace with up-recursion for the function that causes stack
-- overflow. It is recorded first and inlines func_side_trace.
local function stackov_f()
  local r = stackov_f(func_side_trace())
  return r
end

local result, errmsg = pcall(stackov_f)

-- There is no assertion failure if we are here.
-- Just sanity checks.
test:ok(not result, 'correct status')
test:like(errmsg, 'stack overflow', 'correct error message')

test:done(true)
