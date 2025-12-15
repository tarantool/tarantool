local tap = require('tap')
local ffi = require('ffi')

-- This test demonstrates LuaJIT's incorrect emitting of LDP/STP
-- instruction fused from LDR/STR with negative offset and
-- positive offset with the same lower bits on arm64.
-- See also https://github.com/LuaJIT/LuaJIT/pull/1075.
local test = tap.test('lj-1075-arm64-incorrect-ldp-stp-fusion'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(6)

-- Amount of iterations to compile and run the invariant part of
-- the trace.
local N_ITERATIONS = 4

local EXPECTED = 42

-- 4 slots of redzone for int64_t load/store.
local REDZONE = 4
local MASK_IMM7 = 0x7f
local BUFLEN = (MASK_IMM7 + REDZONE) * 4
local buf = ffi.new('unsigned char [' .. BUFLEN .. ']', 0)

local function clear_buf()
  ffi.fill(buf, ffi.sizeof(buf), 0)
end

-- Initialize the buffer with simple values.
local function init_buf()
  -- Limit to fill the buffer. 0 in the top part helps
  -- to detect the issue.
  local LIMIT = BUFLEN - 12
  for i = 0, LIMIT - 1  do
    buf[i] = i
  end
  for i = LIMIT, BUFLEN - 1  do
    buf[i] = 0
  end
end

jit.opt.start('hotloop=1')

-- Assume we have stores/loads from the pointer with offset
-- +488 and -16. The lower 7 bits of the offset (-16) >> 2 are
-- 1111100. These bits are the same as for the offset (488 + 8).
-- Thus, before the patch, these two instructions:
-- | str   x20, [x21, #488]
-- | stur  x20, [x21, #-16]
-- are incorrectly fused to the:
-- | stp   x20, x20, [x21, #488]

-- Test stores.

local start = ffi.cast('unsigned char *', buf)
-- Use constants to allow optimization to take place.
local base_ptr = start + 16
for _ = 1, N_ITERATIONS do
  -- Save the result only for the last iteration.
  clear_buf()
  -- These 2 accesses become `base_ptr + 488` and `base_ptr + 496`
  -- on the trace before the patch.
  ffi.cast('uint64_t *', base_ptr + 488)[0] = EXPECTED
  ffi.cast('uint64_t *', base_ptr - 16)[0] = EXPECTED
end

test:is(buf[488 + 16], EXPECTED, 'correct store top value')
test:is(buf[0], EXPECTED, 'correct store bottom value')

-- Test loads.

init_buf()

local top, bottom
for _ = 1, N_ITERATIONS do
  -- These 2 accesses become `base_ptr + 488` and `base_ptr + 496`
  -- on the trace before the patch.
  top = ffi.cast('uint64_t *', base_ptr + 488)[0]
  bottom = ffi.cast('uint64_t *', base_ptr - 16)[0]
end

test:is(top, 0xfffefdfcfbfaf9f8ULL, 'correct load top value')
test:is(bottom, 0x706050403020100ULL, 'correct load bottom value')

-- Another reproducer that is based on the snapshot restoring.
-- Its advantage is avoiding FFI usage.

-- Snapshot slots are restored in the reversed order.
-- The recording order is the following (from the bottom of the
-- trace to the top):
-- - 0th  (ofs == -16) -- `f64()` replaced the `tail64()` on the
--                         stack,
-- - 63rd (ofs == 488) -- 1,
-- - 64th (ofs == 496) -- 2.
-- At recording, the instructions for the 0th and 63rd slots are
-- merged like the following:
-- | str   x3, [x19, #496]
-- | stp   x2, x1, [x19, #488]
-- The first store is dominated by the stp, so the restored value
-- is incorrect.

-- Function with 63 slots on the stack.
local function f63()
  -- 61 unused slots to avoid extra stores in between.
  -- luacheck: no unused
  local _, _, _, _, _, _, _, _, _, _
  local _, _, _, _, _, _, _, _, _, _
  local _, _, _, _, _, _, _, _, _, _
  local _, _, _, _, _, _, _, _, _, _
  local _, _, _, _, _, _, _, _, _, _
  local _, _, _, _, _, _, _, _, _, _
  local _
  return 1, 2
end

local function tail63()
  return f63()
end

-- Record the trace.
tail63()
tail63()
-- Run the trace.
local one, two = tail63()
test:is(one, 1, 'correct 1st value on stack')
test:is(two, 2, 'correct 2nd value on stack')

test:done(true)
