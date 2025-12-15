local tap = require('tap')

-- Test file to demonstrate LuaJIT misbehaviour for bitshift
-- operations in DUALNUM mode.
-- See also:
-- https://www.freelists.org/post/luajit/dead-loop-in-bitrshift.

local test = tap.test('fix-bit-shift-dualnum')
test:plan(5)

-- This produces the number (not integer) `TValue` type for the
-- DUALNUM build. If the second parameter of any of the shift
-- functions is not an integer in the DUALNUM build, LuaJIT tries
-- to convert it to an integer. In the case of a number, it does
-- nothing and endlessly retries the call to the fallback
-- function.
local SHIFT_V = 1 - '0'

-- Any of the shift calls below causes the infinite FFH retrying
-- loop before the patch.
test:ok(bit.arshift(0, SHIFT_V), 0, 'no infifnite loop in bit.arshift')
test:ok(bit.lshift(0, SHIFT_V), 0, 'no infifnite loop in bit.lshift')
test:ok(bit.rshift(0, SHIFT_V), 0, 'no infifnite loop in bit.rshift')
test:ok(bit.rol(0, SHIFT_V), 0, 'no infifnite loop in bit.rol')
test:ok(bit.ror(0, SHIFT_V), 0, 'no infifnite loop in bit.ror')

test:done(true)
