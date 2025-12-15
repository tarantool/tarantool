local tap = require('tap')
local test = tap.test('lj-624-jloop-snapshot-pc'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)
-- XXX: The test case below triggers the assertion that was
-- added in the patch if tested without the fix itself. It
-- is hard to create a stable reproducer without turning off
-- ASLR and VM randomizations, which is not suitable for testing.

-- Reproducer below produces the following traces:
-- ---- TRACE 1 start test.lua:2
-- 0001  KSHORT   1   2
-- 0002  ISGE     0   1
-- 0003  JMP      1 => 0006
-- 0006  UGET     1   0      ; fib
-- 0007  SUBVN    2   0   0  ; 1
-- 0008  CALL     1   2   2
-- 0000  . FUNCF    4          ; test.lua:2
-- 0001  . KSHORT   1   2
-- 0002  . ISGE     0   1
-- 0003  . JMP      1 => 0006
-- 0006  . UGET     1   0      ; fib
-- 0007  . SUBVN    2   0   0  ; 1
-- 0008  . CALL     1   2   2
-- 0000  . . FUNCF    4          ; test.lua:2
-- 0001  . . KSHORT   1   2
-- 0002  . . ISGE     0   1
-- 0003  . . JMP      1 => 0006
-- 0006  . . UGET     1   0      ; fib
-- 0007  . . SUBVN    2   0   0  ; 1
-- 0008  . . CALL     1   2   2
-- 0000  . . . FUNCF    4          ; test.lua:2
-- ---- TRACE 1 stop -> up-recursion
--
-- ---- TRACE 1 exit 1
-- ---- TRACE 2 start 1/1 test.lua:3
-- 0004  ISTC     1   0
-- 0005  JMP      1 => 0013
-- 0013  RET1     1   2
-- 0009  UGET     2   0      ; fib
-- 0010  SUBVN    3   0   1  ; 2
-- 0011  CALL     2   2   2
-- 0000  . JFUNCF   4   1         ; test.lua:2
-- ---- TRACE 2 stop -> 1
--
-- ---- TRACE 2 exit 1
-- ---- TRACE 3 start 2/1 test.lua:3
-- 0013  RET1     1   2
-- 0012  ADDVV    1   1   2
-- 0013  RET1     1   2
-- ---- TRACE 3 abort test.lua:3 -- down-recursion, restarting
--
-- ---- TRACE 3 start test.lua:3
-- 0013  RET1     1   2
-- 0009  UGET     2   0      ; fib
-- 0010  SUBVN    3   0   1  ; 2
-- 0011  CALL     2   2   2
-- 0000  . JFUNCF   4   1         ; test.lua:2
-- ---- TRACE 3 stop -> 1
--
-- ---- TRACE 2 exit 1
-- ---- TRACE 4 start 2/1 test.lua:3
-- 0013  RET1     1   2
-- 0012  ADDVV    1   1   2
-- 0013  JLOOP    3   3
--
-- The assertion introduced in the previous patch is triggered
-- during recording of the last 0013 JLOOP.
--
-- See also:
-- https://github.com/luaJIT/LuaJIT/issues/624

jit.opt.start('hotloop=1', 'hotexit=1')
local function fib(n)
  return n < 2 and n or fib(n - 1) + fib(n - 2)
end

fib(5)

test:ok(true, 'snapshot pc is correct')
test:done(true)
