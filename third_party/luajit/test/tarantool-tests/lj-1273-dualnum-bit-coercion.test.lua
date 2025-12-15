local tap = require('tap')

-- Test file to demonstrate LuaJIT misbehaviour on operating
-- for 64-bit operands in built-in `bit` library in DUALNUM mode.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/1273.

local test = tap.test('lj-1273-dualnum-bit-coercion')
test:plan(1)

local bit = require('bit')

-- The cdata value for 2 ^ 33.
local EXPECTED = 2 ^ 33 + 0LL
-- Same value is used as mask for `bit.band()`.
local MASK = EXPECTED

test:is(bit.band(2 ^ 33, MASK), EXPECTED, 'correct bit.band result')

test:done(true)
