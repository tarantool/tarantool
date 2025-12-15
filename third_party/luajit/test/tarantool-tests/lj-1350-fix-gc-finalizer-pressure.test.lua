local tap = require('tap')
local ffi = require('ffi')

-- Test file to demonstrate too fast memory growth without
-- shrinking the finalizer table for cdata at the end of the GC
-- cycle.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1350.

local test = tap.test('lj-1350-fix-gc-finalizer-pressure'):skipcond({
  -- Tarantool has its own cdata objects. This makes the test
  -- unpredictable. Disable it.
  ['Disable test for Tarantool'] = _TARANTOOL,
  ['Test requires GC64 mode enabled'] = not ffi.abi('gc64'),
  ['Disabled with Valgrind (Timeout)'] = os.getenv('LUAJIT_TEST_USE_VALGRIND'),
})

test:plan(1)

-- This stress test shows the memory overconsumption if LuaJIT
-- does not rehash the table with cdata finalizers at the end of
-- the GC cycle. Without reshashing of this table, it increases
-- the estimated amount of memory for the GC. Hence, with the
-- bigger `estimate`, the threshold before starting the GC cycle
-- is increased too. This allows allocating more cdata objects and
-- increasing the size of the finalizer table again. This
-- increases the memory estimate again and so on. As a result, we
-- have unlimited memory growth without rehashing of the table for
-- the cdata-intensive workloads.

local cnt = 0
-- Finalizer function for cdata object.
local function dec() cnt = cnt - 1 end

local function new_obj()
  -- Use an array type to make it possible to set a GC finalizer.
  local obj = ffi.new('uint64_t [1]')
  -- Insert the object into the finalizer table.
  ffi.gc(obj, dec)
  cnt = cnt + 1
  -- The empirical assertion check. Without rehashing, there are
  -- more cdata objects than the mentioned limit.
  assert(cnt < 10e6, 'too much cdata alive at the moment')
  return obj
end

-- Reset the GC.
collectgarbage('collect')

local t = {}
-- Don't use too huge limit to avoid the test run being too long.
-- But it is big enough to trigger the assertion above without
-- cdata finalizer table rehashing at the end of the GC cycle.
for i = 1, 3e7 do
  table.insert(t, new_obj())
  -- Reset the table. Now cdata objects are collectable.
  if i % 3.5e6 == 0 then
    t = {}
  end
end

test:ok(true, 'not too much cdata alive at the moment')
test:done(true)
