local tap = require('tap')

local test = tap.test('tonumber-negative-non-decimal-base')
test:plan(18)

-- Test valid tonumber() with +- signs and non-10 base.
test:ok(tonumber('-010', 2) == -2, 'negative base 2')
test:ok(tonumber('-10', 8) == -8, 'negative base 8')
test:ok(tonumber('-0x10', 16) == -16, 'negative base 16')
test:ok(tonumber('  -1010  ', 2) == -10, 'negative base 2 with spaces')
test:ok(tonumber('  +1010  ', 2) == 10, 'positive base 2 with spaces')
test:ok(tonumber('  -012  ', 8) == -10, 'negative base 8 with spaces')
test:ok(tonumber('  +012  ', 8) == 10, 'positive base 8 with spaces')
test:ok(tonumber('  -10  ', 16) == -16, 'negative base 16 with spaces')
test:ok(tonumber('  +10  ', 16) == 16, 'positive base 16 with spaces')
test:ok(tonumber('  -1Z  ', 36) == -36 - 35, 'negative base 36 with spaces')
test:ok(tonumber('  +1z  ', 36) == 36 + 35, 'positive base 36 with spaces')
test:ok(tonumber('-fF', 16) == -(15 + (16 * 15)), 'negative base 16 mixed case')
test:ok(tonumber('-0ffffffFFFF', 16) - 1 == -2 ^ 40, 'negative base 16 long')

-- Test invalid tonumber() for non-10 base.
test:ok(tonumber('-z1010  ', 2) == nil, 'incorrect notation in base 2')
test:ok(tonumber('--1010  ', 2) == nil, 'double minus sign')
test:ok(tonumber('-+1010  ', 2) == nil, 'minus plus sign')
test:ok(tonumber('- 1010  ', 2) == nil, 'space between sign and value')
test:ok(tonumber('-_1010  ', 2) == nil,
	'invalid character between sign and value')

test:done(true)
