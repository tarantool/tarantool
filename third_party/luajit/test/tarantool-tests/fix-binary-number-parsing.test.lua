local tap = require('tap')

-- Test file to demonstrate incorrect behaviour of binary number
-- parsing with fractional dot.
-- See also:
-- luacheck: push no max_comment_line_length
-- https://www.freelists.org/post/luajit/Fractional-binary-number-literals
-- luacheck: pop
local test = tap.test('fix-binary-number-parsing')
test:plan(2)

-- Test that an incorrect literal with a non-0 fractional part
-- still can't be converted to a number.
test:is(tonumber('0b.1'), nil, '0b.1 is not converted')
-- Test that an incorrect literal with 0 fractional part can't be
-- converted to a number.
test:is(tonumber('0b.0'), nil, '0b.0 is not converted')

test:done(true)
