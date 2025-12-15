local tap = require('tap')

local test = tap.test("Tarantool 4773")
test:plan(3)

-- Test file to demonstrate LuaJIT tonumber routine fails on NUL
-- char, details:
--     https://github.com/tarantool/tarantool/issues/4773

local t = {
  zero = '0',
  null = '\x00',
  tail = 'imun',
}

-- Since VM, Lua/C API and compiler use a single routine for
-- conversion numeric string to a number, test cases are reduced
-- to the following:
test:is(tonumber(t.zero), 0)
test:is(tonumber(t.zero .. t.tail), nil)
test:is(tonumber(t.zero .. t.null .. t.tail), nil)

test:done(true)
