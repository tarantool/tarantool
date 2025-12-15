local tap = require('tap')

-- Test file to demonstrate consistent behaviour for JIT and the
-- VM regarding FMA optimization (disabled by default).
-- XXX: The VM behaviour is checked in the
-- <lj-918-fma-numerical-accuracy.test.lua>.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/918.
local test = tap.test('lj-918-fma-numerical-accuracy-jit'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local _2pow52 = 2 ^ 52

-- XXX: Before this commit the LuaJIT arm64 VM uses `fmsub` [1]
-- instruction for the modulo operation, which is the fused
-- multiply-add (FMA [2]) operation (more precisely,
-- multiply-sub). Hence, it may produce different results compared
-- to the unfused one. For the test, let's just use 2 numbers in
-- modulo for which the single rounding is different from the
-- double rounding. The numbers from the original issue are good
-- enough.
--
-- luacheck: push no max_comment_line_length
--
-- [1]:https://developer.arm.com/documentation/dui0801/g/A64-Floating-point-Instructions/FMSUB
-- [2]:https://en.wikipedia.org/wiki/Multiply%E2%80%93accumulate_operation
--
-- luacheck: pop
--
-- IEEE754 components to double:
-- sign * (2 ^ (exp - 1023)) * (mantissa / _2pow52 + normal).
local a = 1 * (2 ^ (1083 - 1023)) * (4080546448249347 / _2pow52 + 1)
assert(a == 2197541395358679800)

local b = -1 * (2 ^ (1052 - 1023)) * (3927497732209973 / _2pow52 + 1)
assert(b == -1005065126.3690554)

local results = {}

jit.opt.start('hotloop=1')
for i = 1, 4 do
  results[i] = a % b
end

-- XXX: The test doesn't fail before this commit. But it is
-- required to be sure that there are no inconsistencies after the
-- commit.
test:samevalues(results, 'consistent behaviour between the JIT and the VM')

test:done(true)
