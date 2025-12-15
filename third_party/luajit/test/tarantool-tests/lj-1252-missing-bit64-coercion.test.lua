local tap = require('tap')

-- Test file to demonstrate LuaJIT incorrect recording of
-- `bit` library with needed coercion from string.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1252.

local test = tap.test('lj-1252-missing-bit64-coercion'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- Simplify the `jit.dump()` output.
local bor = bit.bor

jit.opt.start('hotloop=1')

-- Before the patch, with the missed coercion from string, there
-- is the cast to `i64` from `p64`, where the last one is the
-- string address. This leads to an incorrect result of the bit
-- operation.

local results = {}
for i = 1, 4 do
  results[i] = bor('0', 0LL)
end

test:samevalues(results, 'correct recording of the bit operation with coercion')

test:done(true)
