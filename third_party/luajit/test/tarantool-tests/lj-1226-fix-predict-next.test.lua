local tap = require('tap')
local test = tap.test('lj-1226-fix-predict-next')

test:plan(3)

-- The resulting bytecode is the following:
--
-- 0001    KNIL     0   3
-- 0002    JMP      4 => 0003
-- 0003 => ITERC    4   2   3
-- 0004    ITERL    4 => 0003
--
-- The parsing of the `for` iterator uses the incorrect check for
-- `fs->bclim`, which allows the usage of an uninitialized value,
-- so the test fails under Valgrind.
local res_f = loadstring([[
-- This local variable is necessary, because it emits `KPRI`
-- bytecode, with which the next `KPRI` bytecode will be merged.
local _
for _ in nil do end
]])

test:ok(res_f, 'chunk loaded successfully')

local res, err = pcall(res_f)

-- Check consistency with PUC Rio Lua 5.1 behaviour.
test:ok(not res, 'loaded function not executed')
test:like(err, 'attempt to call a nil value', 'correct error message')

test:done(true)
