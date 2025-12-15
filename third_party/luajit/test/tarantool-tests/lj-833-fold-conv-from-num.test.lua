local tap = require('tap')

-- XXX: Test the behaviour of fold optimizations from numbers to
-- different integer types. The test itself doesn't fail before
-- the commit since these changes relate only to version 2.0.

local test = tap.test('lj-833-fold-conv-from-num'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local ffi = require('ffi')

test:plan(3)

local arr_i64 = ffi.new('int64_t  [2]')
local arr_u64 = ffi.new('uint64_t [2]')
local arr_u32 = ffi.new('uint32_t [2]')

jit.opt.start('hotloop=1')

for _ = 1, 4 do
  -- Test conversion to type (at store). Also, check the
  -- conversion from number to int64_t at C array indexing.
  arr_i64[1.1] = 1.1
  arr_u64[1.1] = 1.1
  arr_u32[1.1] = 1.1
end

test:is(arr_i64[1], 1LL,  'correct conversion to int64_t')
test:is(arr_u64[1], 1ULL, 'correct conversion to uint64_t')
test:is(arr_u32[1], 1ULL, 'correct conversion to uint32_t')

test:done(true)
