local tap = require('tap')
local ffi = require('ffi')
local test = tap.test('lj-745-ffi-typeinfo-dead-names')

test:plan(1)

-- Start from the beginning of the GC cycle.
collectgarbage()

local function ctypes_iteration()
  local i = 1
  -- Test `checklivetv()` assertion in `setstrV()` inside
  -- `ffi.typeinfo()`.
  while ffi.typeinfo(i) do i = i + 1 end
end

-- Call `ffi.typeinfo()` much enough to be sure that strings with
-- names of types become dead. The number of iterations is big
-- enough (more than x2 of the required value) to see assertion
-- failure guaranteed under Tarantool too (it has much more alive
-- objects on start).
for _ = 1, 100 do
  ctypes_iteration()
end

test:ok(true, 'no assertion failure inside ffi.typeinfo()')

test:done(true)
