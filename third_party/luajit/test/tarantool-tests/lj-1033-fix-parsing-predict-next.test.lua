local tap = require('tap')
local test = tap.test('lj-1033-fix-parsing-predict-next')

test:plan(3)

-- The resulting bytecode is the following:
--
-- 0001    KNIL     0   1
-- 0002    MOV      2   1
-- 0003    TGETS    1   1   0  ; "foo"
-- 0004    CALL     1   4   2
--
-- This MOV doesn't use any variable value from the stack, so the
-- attempt to get the name in `predict_next() leads to the crash.
local res_f = loadstring([[
-- This local variable is necessary, because it emits `KPRI`
-- bytecode, with which the next `KPRI` bytecode will be merged.
local _
for _ in (nil):foo() do end
]])

test:ok(res_f, 'chunk loaded successfully')

local res, err = pcall(res_f)

-- Check consistency with PUC Rio Lua 5.1 behaviour.
test:ok(not res, 'loaded function not executed')
test:like(err, 'attempt to index a nil value', 'correct error message')

test:done(true)
