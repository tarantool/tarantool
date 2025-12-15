local tap = require('tap')

-- Test file to demonstrate possible numerical inaccuracy if FMA
-- optimization takes place.
-- XXX: The JIT consistency is checked in the
-- <lj-918-fma-numerical-accuracy-jit.test.lua>.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/918.
local test = tap.test('lj-918-fma-numerical-accuracy')

test:plan(2)

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

-- These tests fail on ARM64 before this patch or with FMA
-- optimization enabled.
-- The first test may not fail if the compiler doesn't generate
-- an ARM64 FMA operation in `lj_vm_foldarith()`.
test:is(2197541395358679800 % -1005065126.3690554, -606337536,
        'FMA in the lj_vm_foldarith() during parsing')

test:is(a % b, -606337536, 'FMA in the VM')

test:done(true)
