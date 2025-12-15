local tap = require('tap')
local test = tap.test('gh-6163-jit-min-max'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(18)
--
-- gh-6163: math.min/math.max inconsistencies.
--

-- This function creates dirty values on the Lua stack.
-- The latter of them is going to be treated as an
-- argument by the `math.min/math.max`.
-- The first two of them are going to be overwritten
-- by the math function itself.
local function filler()
  return 1, 1, 1
end

local min = math.min
local max = math.max

-- It would be enough to test all cases for the
-- `math.min()` or for the `math.max()` only, because the
-- problem was in the common code. However, we shouldn't
-- make such assumptions in the testing code.

-- `math.min()/math.max()` should raise an error when
-- called with no arguments.
filler()
local r, _ = pcall(function() min() end)
test:ok(not r, 'math.min fails with no args')

filler()
r, _ = pcall(function() max() end)
test:ok(false == r, 'math.max fails with no args')

local nan = 0/0
local x = 1

jit.opt.start('hotloop=1')

-- XXX: Looping over the operations and their arguments breaks the
-- semantics of some optimization tests below. The cases are
-- copy-pasted to preserve optimization semantics.

-- Without the `(a o b) o a ==> a o b` fold optimization for
-- `math.min()/math.max()` the following mcode is emitted on
-- aarch64 for the `math.min(math.min(x, nan), x)` expression:
--
-- | fcmp d2, d3 ; fcmp 1.0, nan
-- | fcsel d1, d2, d3, cc ; d1 == nan after this instruction
-- | ...
-- | fcmp d1, d2 ; fcmp nan, 1.0
-- | fcsel d0, d1, d2, cc ; d0 == 1.0 after this instruction
--
-- According to the `fcmp` docs[1], if either of the operands is
-- NaN, then the operands are unordered. It results in the
-- following state of the flags register: N=0, Z=0, C=1, V=1
--
-- According to the `fcsel` docs[2], if the condition is met, then
-- the first register value is taken, otherwise -- the second.
-- In our case, the condition is cc, which means that the `C` flag
-- should be clear[3], which is false. Then, the second value is
-- taken, which is `NaN` for the first `fcmp`-`fcsel` pair, and
-- `1.0` for the second.
--
-- If that fold optimization is applied, then only the first
-- `fcmp`-`fcsel` pair is emitted, and the result is `NaN`, which
-- is inconsistent with the result of the non-optimized mcode.
--
-- luacheck: push no max_comment_line_length
--
-- [1]: https://developer.arm.com/documentation/dui0801/g/A64-Floating-point-Instructions/FCMP
-- [2]: https://developer.arm.com/documentation/100069/0608/A64-Floating-point-Instructions/FCSEL
-- [3]: https://developer.arm.com/documentation/dui0068/b/ARM-Instruction-Reference/Conditional-execution
--
-- luacheck: pop

local result = {}
for k = 1, 4 do
    result[k] = min(min(x, nan), x)
end
-- expected: 1 1 1 1
test:samevalues(result, 'math.min: reassoc_dup')

result = {}
for k = 1, 4 do
    result[k] = max(max(x, nan), x)
end
-- expected: 1 1 1 1
test:samevalues(result, 'math.max: reassoc_dup')

-- If one gets the expression like
-- `math.min(x, math.min(x, nan))`, and the `comm_dup`
-- optimization is applied, it results in the same situation as
-- explained above. With the `comm_dup_minmax` there is no swap,
-- hence, everything is consistent again:
--
-- | fcmp d2, d3 ; fcmp 1.0, nan
-- | fcsel d1, d3, d2, pl ; d1 == nan after this instruction
-- | ...
-- | fcmp d2, d1 ; fcmp 1.0, nan
-- | fcsel d0, d1, d2, pl ; d0 == nan after this instruction
-- `pl` (aka `CC_PL`) condition means that N flag is 0 [2], that
-- is true when we are comparing something with NaN. So, the value
-- of the first source register is taken

result = {}
for k = 1, 4 do
    result[k] = min(x, min(x, nan))
end
-- See also https://github.com/LuaJIT/LuaJIT/issues/957.
-- expected: nan nan nan nan
test:samevalues(result, 'math.min: comm_dup_minmax')

result = {}
for k = 1, 4 do
    result[k] = max(x, max(x, nan))
end
-- See also https://github.com/LuaJIT/LuaJIT/issues/957.
-- expected: nan nan nan nan
test:samevalues(result, 'math.max: comm_dup_minmax')

-- The following optimization should be disabled:
-- (x o k1) o k2 ==> x o (k1 o k2)

x = 1.2
result = {}
for k = 1, 4 do
    result[k] = min(min(x, 0/0), 1.3)
end
-- expected: 1.3 1.3 1.3 1.3
test:samevalues(result, 'math.min: reassoc_minmax_k')

result = {}
for k = 1, 4 do
    result[k] = max(max(x, 0/0), 1.1)
end
-- expected: 1.1 1.1 1.1 1.1
test:samevalues(result, 'math.max: reassoc_minmax_k')

result = {}
for k = 1, 4 do
  result[k] = min(max(nan, 1), 1)
end
-- expected: 1 1 1 1
test:samevalues(result, 'min-max-case1: reassoc_minmax_left')

result = {}
for k = 1, 4 do
  result[k] = min(max(1, nan), 1)
end
-- expected: 1 1 1 1
test:samevalues(result, 'min-max-case2: reassoc_minmax_left')

result = {}
for k = 1, 4 do
  result[k] = max(min(nan, 1), 1)
end
-- expected: 1 1 1 1
test:samevalues(result, 'max-min-case1: reassoc_minmax_left')

result = {}
for k = 1, 4 do
  result[k] = max(min(1, nan), 1)
end
-- expected: 1 1 1 1
test:samevalues(result, 'max-min-case2: reassoc_minmax_left')

result = {}
for k = 1, 4 do
  result[k] = min(1, max(nan, 1))
end
-- expected: 1 1 1 1
test:samevalues(result, 'min-max-case1: reassoc_minmax_right')

result = {}
for k = 1, 4 do
  result[k] = min(1, max(1, nan))
end
-- See also https://github.com/LuaJIT/LuaJIT/issues/957.
-- expected: nan nan nan nan
test:samevalues(result, 'min-max-case2: reassoc_minmax_right')

result = {}
for k = 1, 4 do
  result[k] = max(1, min(nan, 1))
end
-- expected: 1 1 1 1
test:samevalues(result, 'max-min-case1: reassoc_minmax_right')

result = {}
for k = 1, 4 do
  result[k] = max(1, min(1, nan))
end
-- See also https://github.com/LuaJIT/LuaJIT/issues/957.
-- expected: nan nan nan nan
test:samevalues(result, 'max-min-case2: reassoc_minmax_right')

-- XXX: If we look into the disassembled code of
-- `lj_vm_foldarith()` we can see the following:
--
-- luacheck: push no max_comment_line_length
--
-- | /* In our test x == 7.1, y == nan */
-- | case IR_MIN - IR_ADD: return x > y ? y : x; break;
--
-- | ; case IR_MIN
-- | <lj_vm_foldarith+337>: movsd xmm0,QWORD PTR [rsp+0x18] ; xmm0 <- 7.1
-- | <lj_vm_foldarith+343>: comisd xmm0,QWORD PTR [rsp+0x10] ; comisd 7.1, nan
-- | <lj_vm_foldarith+349>: jbe <lj_vm_foldarith+358> ; >= ?
-- | <lj_vm_foldarith+351>: mov rax,QWORD PTR [rsp+0x10] ; return nan
-- | <lj_vm_foldarith+356>: jmp <lj_vm_foldarith+398> ;
-- | <lj_vm_foldarith+358>: mov rax,QWORD PTR [rsp+0x18] ; else return 7.1
-- | <lj_vm_foldarith+363>: jmp <lj_vm_foldarith+398> ;
--
-- luacheck: pop
--
-- According to `comisd` documentation [4] in case when one of the
-- operands is NaN, the result is unordered and ZF,PF,CF := 111.
-- This means that `jbe` condition is true (CF=1 or ZF=1)[5], so
-- we return 7.1 (the first operand) for case `IR_MIN`.
--
-- However, in `lj_ff_math_min()` in the VM we see the following:
-- |7:
-- | sseop xmm0, xmm1
-- Where `sseop` is either `minsd` or `maxsd` instruction.
-- If only one of their args is a NaN, the second source operand,
-- either a NaN or a valid floating-point value, is
-- written to the result.
--
-- So the patch changes the `lj_vm_foldairth()` assembly in the
-- following way:
--
-- luacheck: push no max_comment_line_length
--
-- | ; case IR_MIN
-- | <lj_vm_foldarith+337>: movsd xmm0,QWORD PTR [rsp+0x10] ; xmm0 <- nan
-- | <lj_vm_foldarith+343>: comisd xmm0,QWORD PTR [rsp+0x18] ; comisd nan, 7.1
-- | <lj_vm_foldarith+349>: jbe <lj_vm_foldarith+358> ; >= ?
-- | <lj_vm_foldarith+351>: mov rax,QWORD PTR [rsp+0x18] ; return 7.1
-- | <lj_vm_foldarith+356>: jmp <lj_vm_foldarith+398> ;
-- | <lj_vm_foldarith+358>: mov rax,QWORD PTR [rsp+0x10] ; else return nan
-- | <lj_vm_foldarith+363>: jmp <lj_vm_foldarith+398> ;
--
-- luacheck: pop
--
-- So now we always return the second operand.
--
-- XXX: The two tests below use the `0/0` constant instead of
-- `nan` variable is dictated by the `fold_kfold_numarith`
-- semantics.
result = {}
for k = 1, 4 do
  result[k] = min(min(7.1, 0/0), 1.1)
end
-- expected: 1.1 1.1 1.1 1.1
test:samevalues(result, 'min: fold_kfold_numarith')

result = {}
for k = 1, 4 do
  result[k] = max(max(7.1, 0/0), 1.1)
end
-- expected: 1.1 1.1 1.1 1.1
test:samevalues(result, 'max: fold_kfold_numarith')

test:done(true)
