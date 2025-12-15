local tap = require('tap')

-- Test file to demonstrate LuaJIT incorrect recording of
-- `string.format()` function with huge amount of elements.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1203.

local test = tap.test('lj-1203-limit-format-elements'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

jit.opt.start('hotloop=1')

-- XXX: Use a huge amount of format elements to process, which
-- creates a lot of string constants.
local NELEMENTS = 25000
local fmt = ('%'):rep(NELEMENTS * 2)
local expected = ('%'):rep(NELEMENTS)
local result
for _ = 1, 4 do
  result = fmt:format()
end

test:is(result, expected, 'no IR buffer underflow and the correct result')

test:done(true)
