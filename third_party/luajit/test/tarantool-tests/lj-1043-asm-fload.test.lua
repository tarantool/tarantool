local tap = require('tap')

-- Test file to demonstrate LuaJIT's misbehaviour during the
-- assembling of the `FLOAD` on MIPS.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1043.
local test = tap.test('lj-1043-asm-fload'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local math_abs = math.abs

local results = {nil, nil, nil, nil}

-- Disable optimizations to be sure that we assemble `FLOAD`.
jit.opt.start(0, 'hotloop=1')
for i = 1, 4 do
  results[i] = math_abs(i - 10)
end

test:is_deeply(results, {9, 8, 7, 6}, 'correct assembling of the FLOAD')

test:done(true)
