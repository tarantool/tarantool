#!/usr/bin/env tarantool

if #arg == 0 then
  local tap = require('tap')
  local test = tap.test('test')

  test:plan(1)

  -- XXX: Shell argument <test> is necessary to differ test case
  -- from the test runner.
  local cmd = string.gsub('<LUABIN> 2>/dev/null <SCRIPT> test', '%<(%w+)>', {
    LUABIN = arg[-1],
    SCRIPT = arg[0],
  })
  test:isnt(os.execute(cmd), 0, 'fiber.yield is forbidden in __gc')

  os.exit(test:check() and 0 or 1)
end


-- Test body.

local ffi = require('ffi')
local fiber = require('fiber')

ffi.cdef('struct test { int foo; };')

local test = ffi.metatype('struct test', {
  __gc = function() fiber.yield() end,
})

test(9)

-- This call leads to the platform panic.
collectgarbage('collect')
