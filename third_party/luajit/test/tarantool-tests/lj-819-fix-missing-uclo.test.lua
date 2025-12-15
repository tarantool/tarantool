local tap = require('tap')
-- Test contains a reproducer for a problem when LuaJIT generates
-- a wrong bytecode with a missed BC_UCLO instruction.
local test = tap.test('lj-819-fix-missing-uclo'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

-- Let's take a look at listings Listing 1 and Listing 2 below
-- with bytecode generated for a function missing_uclo() with and
-- without a patch.
-- Both listings contains two BC_UCLO instructions:
-- - first one with id 0004 is generated for a statement 'break'
--   inside condition, see label BC_UCLO1;
-- - second one with id 0009 is generated for a statement 'return'
--   inside a nested loop, see label BC_UCLO2;
-- Both BC_UCLO's closes upvalues after leaving a function's
-- scope.
--
-- The problem is happen when fs_fixup_ret() traverses bytecode
-- instructions in a function prototype, meets first BC_UCLO
-- instruction (break) and forgives a second one (return). This
-- leads to a wrong result produced by a function returned by
-- missing_uclo() function. This also explains why do we need a
-- dead code in reproducer - without first BC_UCLO fs_fixup_ret()
-- successfully fixup BC_UCLO and problem does not appear.
--
-- Listing 1. Bytecode with a fix.
--
-- -- BYTECODE -- uclo.lua:1-59
-- 0001 => LOOP     0 => 0013
-- 0002    JMP      0 => 0003
-- 0003 => JMP      0 => 0005
-- 0004    UCLO     0 => 0013
-- 0005 => KPRI     0   0
-- 0006 => LOOP     1 => 0012
-- 0007    ISF          0
-- 0008    JMP      1 => 0010
-- 0009    UCLO     0 => 0014
-- 0010 => FNEW     0   0      ; uclo.lua:54
-- 0011    JMP      1 => 0006
-- 0012 => UCLO     0 => 0001
-- 0013 => RET0     0   1
-- 0014 => RET1     0   2
--
-- Listing 2. Bytecode without a fix.
--
-- BYTECODE -- uclo.lua:1-59
-- 0001 => LOOP     0 => 0013
-- 0002    JMP      0 => 0003
-- 0003 => JMP      0 => 0005
-- 0004    UCLO     0 => 0013
-- 0005 => KPRI     0   0
-- 0006 => LOOP     1 => 0012
-- 0007    ISF          0
-- 0008    JMP      1 => 0010
-- 0009    UCLO     0 => 0014
-- 0010 => FNEW     0   0      ; uclo.lua:54
-- 0011    JMP      1 => 0006
-- 0012 => UCLO     0 => 0001
-- 0013 => RET0     0   1
-- 0014 => RET1     0   2
--
-- Listing 3. Changes in bytecode before and after a fix.
--
-- @@ -11,11 +11,12 @@
--  0006 => LOOP     1 => 0012
--  0007    ISF          0
--  0008    JMP      1 => 0010
-- -0009    RET1     0   2
-- +0009    UCLO     0 => 0014
--  0010 => FNEW     0   0      ; uclo.lua:56
--  0011    JMP      1 => 0006
--  0012 => UCLO     0 => 0001
--  0013 => RET0     0   1
-- +0014 => RET1     0   2
--
-- First testcase checks a correct bytecode generation by frontend
-- and the second testcase checks consistency on a JIT
-- compilation.

local function missing_uclo()
  while true do -- luacheck: ignore
    -- Attention: it is not a dead code, it is a part of
    -- reproducer.
    -- label: BC_UCLO1
    if false then
      break
    end
    local f
    while true do
      if f then
        -- label: BC_UCLO2
        return f
      end
      f = function()
        return f
      end
    end
  end
end

local f = missing_uclo()
local res = f()
-- Without a patch we don't get here a function, because upvalue
-- isn't closed as desirable.
test:ok(type(res) == 'function',
        'virtual machine consistency: type of returned value is correct')

-- Make JIT compiler aggressive.
jit.opt.start('hotloop=1')

f = missing_uclo()
f()
f = missing_uclo()
local _
_, res = pcall(f)
test:ok(type(res) == 'function',
        'consistency on compilation: type of returned value is correct')

test:done(true)
