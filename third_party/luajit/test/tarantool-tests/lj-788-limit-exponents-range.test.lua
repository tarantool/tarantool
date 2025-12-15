local tap = require('tap')

-- Test file to demonstrate incorrect behaviour of exponent number
-- form parsing.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/788.
local test = tap.test('lj-788-limit-exponents-range')
test:plan(3)

-- Before the patch, the powers greater than (1 << 16) * 10
-- (655360) were parsed incorrectly. After the patch, powers
-- greater than 1 << 20 (1048576 `STRSCAN_MAXEXP`) are considered
-- invalid. See <src/lj_strscan.c> for details.
-- Choose the first value between these values and the second
-- value bigger than `STRSCAN_MAXEXP` to check parsing correctness
-- for the first one, and `STRSCAN_ERROR` for the second case.
local PARSABLE_EXP_POWER  = 1000000
local STRSCAN_MAXEXP      = 1048576
local TOO_LARGE_EXP_POWER = 1050000

local function form_exp_string(n)
  return '0.' .. string.rep('0', n - 1) .. '1e' .. tostring(n)
end

test:is(tonumber(form_exp_string(PARSABLE_EXP_POWER)), 1,
        'correct parsing of large exponent before the boundary')

test:is(tonumber(form_exp_string(STRSCAN_MAXEXP)), nil,
        'boundary power of exponent is not parsed')

test:is(tonumber(form_exp_string(TOO_LARGE_EXP_POWER)), nil,
        'too big exponent power after the boundary is not parsed')

test:done(true)
