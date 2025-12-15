local tap = require('tap')

-- Test file to demonstrate LuaJIT inconsistent behaviour for
-- `math.min()` and `math.max()` operations when comparing 0 with
-- -0 in the JIT and the VM.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1082.

local test = tap.test('lj-1082-min-max-0-commutative'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(4)

-- XXX: simplify `jit.dump()` output.
local min, max = math.min, math.max

-- Use local variables to prevent fold optimization.
local minus_zero = -0
local zero = 0

local results_min_pm = {}
local results_min_mp = {}
local results_max_mp = {}
local results_max_pm = {}

jit.opt.start('hotloop=1')
for i = 1, 4 do
  -- The resulting value is the second parameter for comparison.
  -- Use `tostring()` to distinguish 0 and -0.
  results_min_pm[i] = tostring(min(zero, minus_zero))
  results_min_mp[i] = tostring(min(minus_zero, zero))
  results_max_pm[i] = tostring(max(zero, minus_zero))
  results_max_mp[i] = tostring(max(minus_zero, zero))
end

test:samevalues(results_min_pm, 'min(0, -0)')
test:samevalues(results_min_mp, 'min(-0, 0)')
test:samevalues(results_max_pm, 'max(0, -0)')
test:samevalues(results_max_mp, 'max(-0, 0)')

test:done(true)
