local tap = require('tap')
-- Test to demonstrate the incorrect JIT behaviour for different
-- power operation optimizations.
-- See also:
-- https://github.com/LuaJIT/LuaJIT/issues/684,
-- https://github.com/LuaJIT/LuaJIT/issues/817.
local test = tap.test('lj-684-pow-inconsistencies'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local tostring = tostring

test:plan(5)

jit.opt.start('hotloop=1')

-- XXX: Prevent hotcount side effects.
jit.off()
jit.flush()

local res = {}
-- -0 ^ 0.5 = 0. Test the sign with `tostring()`.
-- XXX: use local variable to prevent folding via parser.
-- XXX: use stack slot out of trace to prevent constant folding.
local minus_zero = -0
jit.on()
for i = 1, 4 do
  res[i] = tostring(minus_zero ^ 0.5)
end

-- XXX: Prevent hotcount side effects.
jit.off()
jit.flush()

test:samevalues(res, ('consistent results for folding (-0) ^ 0.5'))

jit.on()
-- -inf ^ 0.5 = inf.
res = {}
local minus_inf = -math.huge
jit.on()
for i = 1, 4 do
  res[i] = minus_inf ^ 0.5
end

-- XXX: Prevent hotcount side effects.
jit.off()
jit.flush()

test:samevalues(res, ('consistent results for folding (-inf) ^ 0.5'))

-- 2921 ^ 0.5 = 0x1.b05ec632536fap+5.
res = {}
-- This number has no special meaning and is used as one that
-- gives different results when its square root is obtained with
-- glibc's `sqrt()` and `pow()` operations.
-- XXX: use local variable to prevent folding via parser.
-- XXX: use stack slot out of trace to prevent constant folding.
local corner_case_pow_05 = 2921
jit.on()
for i = 1, 4 do
  res[i] = corner_case_pow_05 ^ 0.5
end

-- XXX: Prevent hotcount side effects.
jit.off()
jit.flush()

test:samevalues(res, ('consistent results for folding 2921 ^ 0.5'))

-- -948388 ^ 3 = -0x1.7ad0e8ad7439dp+59.
res = {}
-- This number has no special meaning and is used as one that
-- gives different results when its power of 3 is obtained with
-- glibc's `pow()` and `x * x * x` operations.
-- XXX: use local variable to prevent folding via parser.
-- XXX: use stack slot out of trace to prevent constant folding.
local corner_case_pow_3 = -948388
jit.on()
for i = 1, 4 do
  res[i] = corner_case_pow_3 ^ 3
end

-- XXX: Prevent hotcount side effects.
jit.off()
jit.flush()

test:samevalues(res, ('consistent results for int pow (-948388) ^ 3'))

-- Narrowing for non-constant base of power operation.
local function pow(base, power)
  return base ^ power
end

jit.on()

-- Compile function first.
pow(1, 2)
pow(1, 2)

-- We need some value near 1, to avoid an infinite result.
local base = 1.0000000001
local power = 65536 * 3
local resulting_value = pow(base, power)

-- XXX: Prevent hotcount side effects.
jit.off()
jit.flush()

test:is(resulting_value, base ^ power, 'guard for narrowing of power operation')

test:done(true)
