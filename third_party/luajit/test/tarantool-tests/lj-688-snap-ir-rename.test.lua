local tap = require('tap')
local test = tap.test('lj-688-snap-ir-rename'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})
test:plan(1)

jit.opt.start('hotloop=1')

-- IR for the loop below looks like the following
-- before the patch:
--
-- 0021 ------------ LOOP ------------
-- ....              SNAP   #6   [ ---- ---- 0019 false 0007 ]
-- 0022       >  num NE     0019  +1.1
-- ....              SNAP   #7   [ ---- ---- 0019 false 0007 ]
-- 0023 rbp   >  int CONV   0019  int.num
-- 0024       >  int ABC    0013  0023
-- 0025          p32 AREF   0015  0023
-- 0026 xmm6  >  num ALOAD  0025
-- ....              SNAP   #8   [ ---- ---- 0019 false ---- ]
-- 0027       >  num ULE    0026  +0
-- 0028 xmm7   + num ADD    0019  +1
-- ....              SNAP   #9   [ ---- ---- 0019 false ]
-- 0029       >  num LT     0028  +5
-- 0030 xmm7     num PHI    0019  0028
-- 0031 xmm6     nil RENAME 0019  #8
-- ---- TRACE 1 stop -> loop
--
-- RENAME instruction is applied to the 8th snapshot, when it
-- should be applied to the 9th. After the patch that line looks
-- the following way:
--
-- 0031 xmm6     nil RENAME 0019  #9

-- XXX: Note, that reproducer below won't fail on ARM/ARM64
-- even before the patch, because `IR_RENAME` is not emitted
-- for any of the instructions produced.
-- Although it is possible to achieve the same faulty behavior
-- before the patch on ARM/ARM64, it requires a more complex
-- reproducer. Since the code affected by the patch is
-- platform-agnostic, there is no real necessity to test it
-- against ARM/ARM64 separately.
--
-- luacheck: push no max_comment_line_length
-- See also https://drive.google.com/file/d/1iYkFx3F0DOtB9o9ykWfCEm-OdlJNCCL0/view?usp=share_link
-- luacheck: pop


local vals = {-0.1, 0.1, -0.1, 0.1}
local i = 1
local _
while i < 5 do
  assert(i ~= 1.1)
  local l1 = vals[i]
  _ = l1 > 0
  i = i + 1
end

test:ok(true, 'IR_RENAME is fine')
-- `test:check() and 0 or 1` is replaced with just test:check()
-- here, because otherwise, it affects the renaming process.
test:done(true)
