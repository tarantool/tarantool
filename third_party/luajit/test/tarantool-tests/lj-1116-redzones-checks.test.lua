local tap = require('tap')
-- Test file to demonstrate mcode area overflow during recording a
-- trace with the high FPR pressure.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/1116.
--
-- XXX: Test fails with reverted fix and enabled GC64 mode.
local test = tap.test('lj-1116-redzones-checks'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

jit.opt.start('hotloop=1')

-- XXX: This test snippet was originally created by the fuzzer.
-- See https://oss-fuzz.com/testcase-detail/5622965122170880.
--
-- Unfortunately, it's impossible to reduce the testcase further.
-- Before the patch, assembling some instructions (like `IR_CONV
-- int.num`, for example) with many mcode to be emitted may
-- overflow the `MCLIM_REDZONE` (64) at once due to the huge
-- mcode emitting.
-- For example `IR_CONV` in this test requires 66 bytes of the
-- machine code:
-- |  cvttsd2si r15d, xmm5
-- |  xorps xmm9, xmm9
-- |  cvtsi2sd xmm9, r15d
-- |  ucomisd xmm5, xmm9
-- |  jnz 0x11edb00e5       ->37
-- |  jpe 0x11edb00e5       ->37
-- |  mov [rsp+0x80], r15d
-- |  mov r15, [rsp+0xe8]
-- |  movsd xmm9, [rsp+0xe0]
-- |  movsd xmm5, [rsp+0xd8]
--
-- The reproducer needs sufficient register pressure as to
-- immediately spill the result of the instruction to the stack
-- and then reload the three registers used by the instruction,
-- and to have chosen enough registers with numbers >=8 (because
-- shaving off a REX prefix [1] or two would get 66 back down
-- to <= `MCLIM_REDZONE`), and to be using lots of spill slots
-- (because memory offsets <= 0x7f are shorter to encode compared
-- to those >= 0x80. So, each reload instruction consumes 9 bytes.
-- This makes this reproducer unstable (regarding the register
-- allocator changes). So, lets use this as a regression test.
--
-- luacheck: push no max_comment_line_length
-- [1]: https://wiki.osdev.org/X86-64_Instruction_Encoding#REX_prefix
-- luacheck: pop

_G.a = 0
_G.b = 0
_G.c = 0
_G.d = 0
_G.e = 0
_G.f = 0
_G.g = 0
_G.h = 0
-- Skip `i` -- it is used for the loop index.
_G.j = 0
_G.k = 0
_G.l = 0
_G.m = 0
_G.n = 0
_G.o = 0
_G.p = 0
_G.q = 0
_G.r = 0
_G.s = 0
_G.t = 0
_G.u = 0
_G.v = 0
_G.w = 0
_G.x = 0
_G.y = 0
_G.z = 0

-- XXX: Need here not 4, but 4.5 top border of the cycle to create
-- FPR pressure.
for i = 1, 4.5 do
  _G.a = _G.a + 1
  _G.b = _G.b + 1
  _G.c = _G.c + 1
  _G.d = _G.d + 1
  for _ = i, 2 do
    _G.e = _G.e + 1
  end
  -- Here we emit `IR_CONV int.num`. This loop is inlined.
  -- Assertion failed after emitting the variant part of the
  -- big loop.
  for _ = 2, i do
    _G.f = _G.f + 1
    _G.g = _G.g + 1
    _G.h = _G.h + 1
    _G.j = _G.j + 1
    _G.k = _G.k + 1
    _G.l = _G.l + 1
  end
  _G.m = _G.m + 1
  _G.n = _G.n + 1
  _G.o = _G.o + 1
  _G.p = _G.p + 1
  _G.q = _G.q + 1
  _G.r = _G.r + 1
  _G.s = _G.s + 1
  _G.t = _G.t + 1
  _G.u = _G.u + 1
  _G.v = _G.v + 1
  for _ = i, 2.1 do
    _G.aa = _G.a
    _G.w = _G.w + 1
    _G.x = _G.x + 1
    _G.y = _G.y + 1
    _G.z = _G.z + 1
  end
end

test:ok(true, 'no mcode limit assertion failed during recording')

test:done(true)
