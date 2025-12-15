local tap = require('tap')

-- Test file to demonstrate the incorrect LuaJIT's optimization
-- `bufput_append()` for BUFPUT IR.
-- See also https://github.com/LuaJIT/LuaJIT/issues/791.

local test = tap.test('lj-791-fold-bufhdr-append'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local EMPTY_STR = ''
local prefix = 'Lu'
local result

jit.opt.start('hotloop=1')

-- The interesting part of IRs is the following (non-GC64 mode):
-- 0006     str BUFSTR 0005 0003
-- 0007  >  str SLOAD  #2   T
-- 0008     p32 BUFHDR [0x400004a0] RESET
-- 0009     p32 BUFPUT 0008 "Lu"
-- 0010     p32 BUFPUT 0009 0007
-- 0011   + str BUFSTR 0010 0008
-- 0012   + int ADD    0001 +1
-- 0013  >  int LE     0012 +5
-- 0014  >  --- LOOP ------------
-- 0015     p32 BUFHDR [0x400004a0] RESET

-- The instruction to be folded is the following:
-- 0016     p32 BUFPUT 0015 0011
--
-- The 0011 operand is PHI, which is not the last IR in the BUFSTR
-- chain (`ir->prev = REF_BIAS + 0006`). Folding this IR leads to
-- this resulting IR:
-- p32 BUFHDR 0010  APPEND
-- Which appends to buffer instead of resetting, so the resulting
-- string contains one more symbol.

-- XXX: Use 5 iterations to run the variant part of the loop.
for _ = 1, 5 do
  result = prefix .. 'a'
  -- We need a non-constant string to be appended to prevent more
  -- aggressive optimizations. Use an empty string for
  -- convenience. Also, use a constant string in the first operand
  -- in the concatenation operator for more readable `jit.dump`
  -- output.
  prefix = 'Lu' .. EMPTY_STR
end

test:is(result, 'Lua', 'BUFPUT APPEND optimization is not applied for PHIs')

test:done(true)
