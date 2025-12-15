local tap = require('tap')
-- Test file to demonstrate incorrect stitching behaviour
-- in vmevent handler.
-- See also https://github.com/tarantool/tarantool/issues/6782.
local test = tap.test('gh-6782-stitching-in-vmevent-handler'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- Just dump bytecodes is enough.
-- Also, we are not interested in the output.
require('jit.dump').start('-mi', '/dev/null')

-- Make compiler really aggressive.
jit.opt.start('recunroll=1', 'callunroll=1', 'hotloop=1')

-- Function to compile.
local function fibb(n)
  return n < 2 and n or fibb(n - 1) + fibb(n - 2)
end

local function empty() end
-- Compile `jit.bc` functions, that are used in vmevent handler.
require('jit.bc').dump(loadstring(string.dump(fibb)), {
  write = empty,
  close = empty,
  flush = empty,
})

-- Here we dump (to /dev/null) info about `fibb()` traces and run
-- `jit.bc` functions inside.
test:ok(fibb(2) == 1, 'run compiled function inside vmevent handler')

test:done(true)
