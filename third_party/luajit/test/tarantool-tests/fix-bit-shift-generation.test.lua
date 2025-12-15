local tap = require('tap')
local test = tap.test('fix-bit-shift-generation'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local NTESTS = 4

test:plan(NTESTS)

local ffi = require('ffi')
local bit = require('bit')
local rol = bit.rol
local shl = bit.lshift

local testbitshift = ffi.load('testbitshift')
ffi.cdef[[
uint64_t
pick4(const int arg1, const int arg2, const int arg3, const uint64_t arg4)
]]

local result = {}
jit.opt.start('hotloop=1')

for i = 1, NTESTS do
  -- The rotation is performed beyond the 32-bit size, for
  -- truncation to become noticeable. `testbitshift` is used to
  -- ensure that the result of rotation goes into the `rcx`,
  -- corresponding to the x86_64 ABI. Although it is possible to
  -- use a function from the C standard library for that, all of
  -- the suitable ones are variadic, and variadics are recorded
  -- incorrectly on Apple Silicon.
  result[i] = testbitshift.pick4(1, 1, 1, rol(1ULL, i + 32))
  -- Resulting assembly for the `rol` instruction above changes
  -- from the following before the patch:
  -- | rol rsi, cl
  -- | mov ecx, esi
  --
  -- to the following after the patch:
  -- | rol rsi, cl
  -- | mov rcx, rsi
end

for i = 1, NTESTS do
  test:ok(result[i] == shl(1ULL, i + 32), 'valid rol')
end

test:done(true)
