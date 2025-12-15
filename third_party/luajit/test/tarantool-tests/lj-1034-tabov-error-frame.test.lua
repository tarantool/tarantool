local tap = require('tap')
local ffi = require('ffi')
local test = tap.test('lj-1034-tabov-error-frame'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Test requires GC64 mode enabled'] = not ffi.abi('gc64'),
  ['Disabled on MacOS due to #8652'] = jit.os == 'OSX',
  ['Disabled with Valgrind (Timeout)'] = os.getenv('LUAJIT_TEST_USE_VALGRIND'),
})

-- XXX: The test for the problem uses the table of GC
-- finalizers, although they are not required to reproduce
-- the issue. They are only used to make the test as simple
-- as possible.
--
-- XXX: The test requires ~6Gb of memory to see the error.
test:plan(2)

-- luacheck: no unused
local anchor = {}
local function on_gc(t) end

local function test_finalizers()
  local i = 1
  while true do
    anchor[i] = ffi.gc(ffi.cast('void *', 0), on_gc)
    i = i + 1
  end
end

local st, err = pcall(test_finalizers)
st, err = pcall(test_finalizers)
test:ok(st == false, 'error handled successfully')
test:like(err, '^.+table overflow', 'error is table overflow')
test:done(true)
