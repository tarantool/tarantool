local tap = require('tap')

-- Test file to demonstrate the incorrect LuaJIT's behaviour
-- for `math.ceil(x)` when argument `x`: -1 < x < -0.5.
-- See also https://github.com/LuaJIT/LuaJIT/issues/859.

local test = tap.test('lj-859-math-ceil-sign')

test:plan(1)

local IS_DUALNUM = tostring(tonumber('-0')) ~= tostring(-0)
local IS_X86_64 = jit.arch == 'x86' or jit.arch == 'x64'

-- Use `tostring()` to compare the sign of the returned value.
-- Take any value from the mentioned range. The chosen one is
-- mentioned in the commit message.
test:ok((IS_DUALNUM and IS_X86_64) or tostring(math.ceil(-0.9)) == '-0',
        'correct zero sign')

test:done(true)
