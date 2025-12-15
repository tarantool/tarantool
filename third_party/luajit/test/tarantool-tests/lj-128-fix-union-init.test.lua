local tap = require('tap')
local test = tap.test('lj-128-fix-union-init'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local NITERATIONS = 4

test:plan(NITERATIONS)

local ffi = require('ffi')
local union_type = ffi.typeof('union { uint32_t u; float f; }')

-- Before the patch, the `union_type` call resulted in the
-- initialization of both the integer and the float members
-- of the union, leading to undefined behavior since the
-- integer member was overwritten.
-- The IR was the following:
--
-- 0031 ------ LOOP ------------
-- 0032    u8  XLOAD  [0x100684521]  V
-- 0033    int BAND   0032  +12
-- 0034 >  int EQ     0033  +0
-- 0035 >  cdt CNEW   +96
-- 0036    p64 ADD    0035  +16
-- 0037    u32 XSTORE 0036  0029 <--- `u` member init
-- 0038    flt XSTORE 0036  0022 <--- `f` member init
-- 0039    u32 XLOAD  0036
-- 0040    num CONV   0039  num.u32
-- 0041    num CONV   0029  num.int
-- 0042 >  num EQ     0041  0040
-- 0043  + int ADD    0029  +1
-- 0044 >  int LE     0043  0001
-- 0045    int PHI    0029  0043
--
-- After the patch, the initialization is performed only
-- for the first member of the union, so now IR looks
-- like this:
-- 0029 ------ LOOP ------------
-- 0030    u8  XLOAD  [0x1047c4521]  V
-- 0031    int BAND   0030  +12
-- 0032 >  int EQ     0031  +0
-- 0033 }  cdt CNEW   +96
-- 0034    p64 ADD    0033  +16
-- 0035 }  u32 XSTORE 0034  0027 <--- `u` member init
-- 0036    u32 CONV   0027  u32.int
-- 0037    num CONV   0036  num.u32
-- 0038    num CONV   0027  num.int
-- 0039 >  num EQ     0038  0037
-- 0040  + int ADD    0027  +1
-- 0041 >  int LE     0040  0001
-- 0042    int PHI    0027  0040

jit.opt.start('hotloop=1')
for i = 1, NITERATIONS do
  test:ok(union_type(i).u == i, 'first member init only')
end

test:done(true)
