local tap = require('tap')
-- A test file to demonstrate an incorrect recording of
-- `getmetatable()` for I/O handlers.
-- https://github.com/LuaJIT/LuaJIT/issues/1279
local test = tap.test('lj-1279-incorrect-recording-getmetatable'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(6)

jit.opt.start('hotloop=1')

local ud_io_file = io.stdout
local getmetatable = getmetatable

local function rec_getmetatable(obj)
  local res
  for _ = 1, 4 do
    res = getmetatable(obj)
  end
  return res
end

-- The testcase to demonstrate a problem by comparing the
-- metatable returned by two versions of `getmetatable()`:
-- compiled and not.

local mt_orig = debug.getmetatable(ud_io_file)
assert(type(mt_orig) == 'table')

local mt_rec = {}
for i = 1, 4 do
  mt_rec[i] = getmetatable(ud_io_file)
end
mt_rec[5] = mt_orig

test:ok(true, 'getmetatable() recording is correct')
test:samevalues(mt_rec, 'metatables are the same')

-- The testcase to demonstrate a problem by setting the metatable
-- for `io.stdout` to a string.

-- Compile `getmetatable()`, it is expected metatable has
-- a `table` type.
rec_getmetatable(ud_io_file)
-- Set IO metatable to a string.
local mt = 'IO metatable'
getmetatable(ud_io_file).__metatable = mt
test:is(getmetatable(ud_io_file), mt, 'getmetatable() is correct')
test:is(rec_getmetatable(ud_io_file), mt, 'compiled getmetatable() is correct')

-- Restore metatable.
debug.setmetatable(ud_io_file, mt_orig)
assert(type(mt_orig) == 'table')
jit.flush()
jit.opt.start('hotloop=1')

-- The testcase to demonstrate a problem by removing the metatable
-- for `io.stdout` and calling the garbage collector.

-- Compile `getmetatable()`, it is expected metatable has
-- a `table` type.
rec_getmetatable(ud_io_file)
-- Delete metatable.
debug.setmetatable(ud_io_file, nil)
collectgarbage()
test:is(getmetatable(ud_io_file), nil, 'getmetatable() is correct')
test:is(rec_getmetatable(ud_io_file), nil, 'compiled getmetatable() is correct')

-- Restore metatable.
debug.setmetatable(ud_io_file, mt_orig)

test:done(true)
