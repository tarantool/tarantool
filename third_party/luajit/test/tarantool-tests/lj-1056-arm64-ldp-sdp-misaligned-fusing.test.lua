local tap = require('tap')
local ffi = require('ffi')

-- This test demonstrates LuaJIT's incorrect emitting of LDP/STP
-- instructions from LDUR/STUR instructions with misaligned offset
-- on arm64.
-- See also https://github.com/LuaJIT/LuaJIT/issues/1056.
local test = tap.test('lj-1056-arm64-ldp-sdp-misaligned-fusing'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(6)

-- Amount of iterations to compile and run the invariant part of
-- the trace.
local N_ITERATIONS = 4

local EXPECTED = 42

-- 9 bytes to make unaligned 4-byte access like buf + 5.
local BUFLEN = 9
local buf = ffi.new('unsigned char [' .. BUFLEN .. ']', 0)

local function clear_buf()
  ffi.fill(buf, ffi.sizeof(buf), 0)
end

-- Initialize the buffer with simple values.
local function init_buf()
  for i = 0, BUFLEN - 1 do
    buf[i] = i
  end
end

local function test_buf_content(expected_bytes, msg)
  local got_bytes = {}
  assert(#expected_bytes == BUFLEN, 'mismatched size of buffer and table')
  for i = 0, BUFLEN - 1 do
    got_bytes[i + 1] = buf[i]
  end
  test:is_deeply(got_bytes, expected_bytes, msg)
end

jit.opt.start('hotloop=1')

-- Test stores.

for _ = 1, N_ITERATIONS do
  local ptr = ffi.cast('unsigned char *', buf)
  -- These 2 accesses become ptr + 0 and ptr + 4 on the trace
  -- before the patch.
  ffi.cast('int32_t *', ptr + 1)[0] = EXPECTED
  ffi.cast('int32_t *', ptr + 5)[0] = EXPECTED
end
test_buf_content({0, EXPECTED, 0, 0, 0, EXPECTED, 0, 0, 0},
                 'pair of misaligned stores')

clear_buf()

for _ = 1, N_ITERATIONS do
  local ptr = ffi.cast('unsigned char *', buf)
  -- The next access becomes ptr + 4 on the trace before the
  -- patch.
  ffi.cast('int32_t *', ptr + 5)[0] = EXPECTED
  ffi.cast('int32_t *', ptr)[0] = EXPECTED
end
test_buf_content({EXPECTED, 0, 0, 0, 0, EXPECTED, 0, 0, 0},
                 'aligned / misaligned stores')

-- Test loads.

local resl, resr = 0, 0

init_buf()

for _ = 1, N_ITERATIONS do
  local ptr = ffi.cast('unsigned char *', buf)
  -- These 2 accesses become ptr + 0 and ptr + 4 on the trace
  -- before the patch.
  resl = ffi.cast('int32_t *', ptr + 1)[0]
  resr = ffi.cast('int32_t *', ptr + 5)[0]
end

-- Values are resulted from the `init_buf()` function with the
-- corresponding offset.
test:is(resl, 0x4030201, 'pair of misaligned loads, left')
test:is(resr, 0x8070605, 'pair of misaligned loads, right')

for _ = 1, N_ITERATIONS do
  local ptr = ffi.cast('unsigned char *', buf)
  -- The next access becomes ptr + 4 on the trace before the
  -- patch.
  resr = ffi.cast('int32_t *', ptr + 5)[0]
  resl = ffi.cast('int32_t *', ptr)[0]
end

-- Values are resulted from the `init_buf()` function with the
-- corresponding offset.
test:is(resl, 0x3020100, 'aligned / misaligned load, aligned')
test:is(resr, 0x8070605, 'aligned / misaligned load, misaligned')

test:done(true)
