local tap = require('tap')

-- Test file to demonstrate integer overflow in the `unpack()`
-- function due to compiler optimization.
-- See also https://github.com/LuaJIT/LuaJIT/pull/574.
local test = tap.test('lj-574-overflow-unpack')
test:plan(1)

local r, e = pcall(unpack, {}, 0, 2^31 - 1)
test:ok(not r and e == 'too many results to unpack', 'overflow check in unpack')

test:done(true)
