local tap = require('tap')
-- Test to demonstrate the incorrect JIT behaviour when splitting
-- IR_POW.
-- See also https://github.com/LuaJIT/LuaJIT/issues/9.
local test = tap.test('lj-9-pow-inconsistencies'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local nan = 0 / 0
local inf = math.huge

-- Table with some corner cases to check:
-- Not all of them fail on each CPU architecture, but bruteforce
-- is better, than custom enumerated usage for two reasons:
-- * Improved readability.
-- * More extensive and change-proof testing.
local CORNER_CASES = {
  -- 0, -0, 1, -1 special cases with nan, inf, etc..
  0, -0, 1, -1, nan, inf, -inf,
  -- x ^  inf = 0 (inf), if |x| < 1 (|x| > 1).
  -- x ^ -inf = inf (0), if |x| < 1 (|x| > 1).
  0.999999, 1.000001, -0.999999, -1.000001,
  -- Test power of even numbers optimizations.
  2, -2, 0.5, -0.5,
}
test:plan(1 + (#CORNER_CASES) ^ 2)

jit.opt.start('hotloop=1')

-- The JIT engine tries to split b^c to exp2(c * log2(b)).
-- For some cases for IEEE754 we can see, that
-- (double)exp2((double)log2(x)) != x, due to mathematical
-- functions accuracy and double precision restrictions.
-- Just use some numbers to observe this misbehaviour.
local res = {}
for i = 1, 4 do
  -- XXX: use local variable to prevent folding via parser.
  local b = -0.90000000001
  res[i] = 1000 ^ b
end

test:samevalues(res, 'consistent pow operator behaviour for corner case')

-- Prevent JIT side effects for parent loops.
jit.off()
for i = 1, #CORNER_CASES do
  for j = 1, #CORNER_CASES do
    local b = CORNER_CASES[i]
    local c = CORNER_CASES[j]
    local results = {}
    jit.on()
    for k = 1, 4 do
      results[k] = b ^ c
    end
    -- Prevent JIT side effects.
    jit.off()
    jit.flush()
    test:samevalues(
      results,
      ('consistent pow operator behaviour for (%s)^(%s)'):format(b, c)
    )
  end
end

test:done(true)
