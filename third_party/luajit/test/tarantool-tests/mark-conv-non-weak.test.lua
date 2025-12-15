local tap = require('tap')
local test = tap.test('mark-conv-non-weak'):skipcond({
    ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)
-- XXX: These values were chosen to create type instability
-- in the loop-carried dependency, so the checked `CONV int.num`
-- instruction is emitted. See `loop_unrool` in `lj_opt_loop.c`.
local data = {0, 0.1, 0, 0 / 0}

-- XXX: The sum is required to be initialized with a non-zero
-- floating point value.
-- Otherwise, `0023  + num ADD    0017  0007` instruction in the
-- IR below becomes `ADDOV` and the `CONV int.num` conversion is
-- used by it.
local sum = 0.1

jit.opt.start('hotloop=1')

-- XXX: The test fails before the patch only for `DUALNUM` mode.
-- All of the IRs below are produced by the corresponding LuaJIT
-- build.

-- luacheck: push no max_comment_line_length
-- When the trace is recorded, the IR is the following before the
-- patch:
---- TRACE 1 IR
-- ....        SNAP   #0   [ ---- ---- ---- ---- ---- ---- ---- ---- ---- ]
-- 0001    u8  XLOAD  [0x100dac521]  V
-- 0002    int BAND   0001  +12
-- 0003 >  int EQ     0002  +0
-- 0004 >  int SLOAD  #8    T
-- ....        SNAP   #1   [ ---- ---- ---- ---- ---- ---- ---- ---- ---- ]
-- 0005 >  num SLOAD  #3    T
-- 0006    num CONV   0004  num.int
-- 0007  + num ADD    0006  0005
-- 0008 >  fun SLOAD  #4    T
-- 0009 >  tab SLOAD  #5    T
-- 0010 >  int SLOAD  #6    T
-- 0011 >  fun EQ     0008  ipairs_aux
-- 0012  + int ADD    0010  +1
-- 0013    int FLOAD  0009  tab.asize
-- 0014 >  int ABC    0013  0012
-- 0015    p64 FLOAD  0009  tab.array
-- 0016    p64 AREF   0015  0012
-- 0017 >+ num ALOAD  0016
-- ....        SNAP   #2   [ ---- ---- ---- 0007 ---- ---- 0012 0012 0017 ]
-- 0018 ------ LOOP ------------
-- 0019    u8  XLOAD  [0x100dac521]  V
-- 0020    int BAND   0019  +12
-- 0021 >  int EQ     0020  +0
-- 0022 >  int CONV   0017  int.num
-- ....        SNAP   #3   [ ---- ---- ---- 0007 ---- ---- 0012 0012 0017 ]
-- 0023  + num ADD    0017  0007
-- 0024  + int ADD    0012  +1
-- 0025 >  int ABC    0013  0024
-- 0026    p64 AREF   0015  0024
-- 0027 >+ num ALOAD  0026
-- 0028    num PHI    0017  0027
-- 0029    num PHI    0007  0023
-- 0030    int PHI    0012  0024
---- TRACE 1 stop -> loop

---- TRACE 1 exit 0
---- TRACE 1 exit 3
--
-- And the following after the patch:
---- TRACE 1 IR
-- ....        SNAP   #0   [ ---- ---- ---- ---- ---- ---- ---- ---- ---- ]
-- 0001    u8  XLOAD  [0x102438521]  V
-- 0002    int BAND   0001  +12
-- 0003 >  int EQ     0002  +0
-- 0004 >  int SLOAD  #8    T
-- ....        SNAP   #1   [ ---- ---- ---- ---- ---- ---- ---- ---- ---- ]
-- 0005 >  num SLOAD  #3    T
-- 0006    num CONV   0004  num.int
-- 0007  + num ADD    0006  0005
-- 0008 >  fun SLOAD  #4    T
-- 0009 >  tab SLOAD  #5    T
-- 0010 >  int SLOAD  #6    T
-- 0011 >  fun EQ     0008  ipairs_aux
-- 0012  + int ADD    0010  +1
-- 0013    int FLOAD  0009  tab.asize
-- 0014 >  int ABC    0013  0012
-- 0015    p64 FLOAD  0009  tab.array
-- 0016    p64 AREF   0015  0012
-- 0017 >+ num ALOAD  0016
-- ....        SNAP   #2   [ ---- ---- ---- 0007 ---- ---- 0012 0012 0017 ]
-- 0018 ------ LOOP ------------
-- 0019    u8  XLOAD  [0x102438521]  V
-- 0020    int BAND   0019  +12
-- 0021 >  int EQ     0020  +0
-- 0022 >  int CONV   0017  int.num
-- ....        SNAP   #3   [ ---- ---- ---- 0007 ---- ---- 0012 0012 0017 ]
-- 0023  + num ADD    0017  0007
-- 0024  + int ADD    0012  +1
-- 0025 >  int ABC    0013  0024
-- 0026    p64 AREF   0015  0024
-- 0027 >+ num ALOAD  0026
-- 0028    num PHI    0017  0027
-- 0029    num PHI    0007  0023
-- 0030    int PHI    0012  0024
---- TRACE 1 stop -> loop

---- TRACE 1 exit 0
---- TRACE 1 exit 2
--
-- luacheck: pop
--
-- Before the patch, the `0022 >  int CONV   0017  int.num`
-- instruction is omitted due to DCE, which results in the
-- third side exit being taken, instead of the second,
-- and, hence, incorrect summation. After the patch, `CONV`
-- is left intact and is not omitted; it remains as a guarded
-- instruction, so the second side exit is taken and sum is
-- performed correctly.
--
-- Note that DCE happens on the assembly part of the trace
-- compilation. That is why `CONV` is present in both IRs.

for _, val in ipairs(data) do
    if val == val then
        sum = sum + val
    end
end

test:ok(sum == sum, 'NaN check was not omitted')
test:done(true)
