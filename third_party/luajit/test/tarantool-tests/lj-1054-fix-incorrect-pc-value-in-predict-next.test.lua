local tap = require('tap')
local test = tap.test('lj-1054-fix-incorrect-pc-value-in-predict_next')
test:plan(3)

-- The test demonstrates a problem with out-of-boundary
-- access to a stack. The problem can be easily observed
-- on execution of the sample by LuaJIT instrumented by ASAN,
-- where the sanitizer reports a heap-based buffer overflow.
-- See also https://github.com/LuaJIT/LuaJIT/issues/1054.

local res_f = loadstring([[
a, b, c = 1, 2, 3
local d
for _ in nil do end
]])

test:ok(res_f, 'chunk loaded successfully')

local res, err = pcall(res_f)

-- Check consistency with PUC Rio Lua 5.1 behaviour.
test:ok(not res, 'loaded function is failed (expected)')
test:like(err, 'attempt to call a nil value', 'correct error message')

test:done(true)
