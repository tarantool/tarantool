local tap = require('tap')

-- Test file to demonstrate LuaJIT's incorrect fold optimization
-- x - (-0) ==> x.
-- See also https://github.com/LuaJIT/LuaJIT/issues/783.
local test = tap.test('lj-783-fold--0'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

-- XXX: Use the variable to avoid folding during parsing.
local minus_zero = -0
local results = {}

jit.opt.start('hotloop=1')

for i = 1, 4 do
  results[i] = tostring(minus_zero - (-0))
end

-- Fold optimization x - (-0) ==> x is INVALID for x = -0 in FP
-- arithmetic. Its result is -0 instead of +0.

test:is(results[1], '0', 'correct VM value for -0 - (-0)')
test:samevalues(results, '-0 folding in simplify_numsub_k')

test:done(true)
