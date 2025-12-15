local tap = require('tap')
local test = tap.test('lj-375-ir-bufput-signed-char'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

-- XXX: Number of loop iterations.
-- 1 -- instruction becomes hot
-- 2, 3 -- trace is recorded (considering loop recording
--         specifics), but bytecodes are still executed via VM
-- 4 -- trace is executed, need to check that emitted mcode is
--      correct
local NTEST = 4
test:plan(NTEST)

-- Storage for the results to avoid trace aborting by `test:ok()`.
local results = {}
-- Avoid store forwarding optimization to store exactly 1 char.
jit.opt.start(3, '-fwd', 'hotloop=1')
for _ = 1, NTEST do
  -- Check optimization for storing a single char works correct
  -- for 0xff. Fast function `string.char()` is recorded with
  -- IR_BUFHDR and IR_BUFPUT IRs in case, when there are more than
  -- 1 argument.
  local s = string.char(0xff, 0)
  table.insert(results, s:byte(1))
end

for i = 1, NTEST do
  test:ok(results[i] == 0xff, 'correct 0xff signed char assembling')
end

test:done(true)
