local tap = require('tap')
local ffi = require('ffi')

-- This test demonstrates LuaJIT's incorrect emitting of LDP
-- instruction with negative offset fused from LDR on arm64.
-- See also https://github.com/LuaJIT/LuaJIT/pull/1028.
local test = tap.test('lj-1028-ldr-fusion-to-ldp-negative-offset'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- Amount of iterations to compile and start the trace.
local N_ITERATIONS = 4

ffi.cdef[[
  typedef struct data {
    int32_t m1;
    int32_t m2;
  } data;
]]

local data_arr = ffi.new('data[' .. N_ITERATIONS .. ']')

local const_data_ptr = ffi.typeof('const data *')
local data = ffi.cast(const_data_ptr, data_arr)

local results = {}

jit.opt.start('hotloop=1')

for i = 1, N_ITERATIONS do
  -- Pair loading from the negative offset generates an invalid
  -- instruction on AArch64 before this patch.
  local field = data[i - 1]
  local m1 = field.m1
  local m2 = field.m2

  -- Use loaded values to avoid DCE.
  results[i] = m1 + m2
end

test:samevalues(results, 'no invalid instruction')

test:done(true)
