local tap = require('tap')

local test = tap.test('lj-726-profile-flush-close'):skipcond({
  -- See also https://github.com/tarantool/tarantool/issues/10803.
  ['Disabled due to #10803'] = os.getenv('LUAJIT_TEST_USE_VALGRIND'),
})
test:plan(1)

local TEST_FILE = 'lj-726-profile-flush-close.profile'

local function payload()
  local r = 0
  for i = 1, 1e8 do
    r = r + i
  end
  return r
end

local p = require('jit.p')
p.start('f', TEST_FILE)
payload()
p.stop()

local f, err = io.open(TEST_FILE)
assert(f, err)

-- Check that file is not empty.
test:ok(f:read(0), 'profile output was flushed and closed')

assert(os.remove(TEST_FILE))

test:done(true)
