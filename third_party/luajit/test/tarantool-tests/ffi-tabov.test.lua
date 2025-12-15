local tap = require('tap')
local ffi = require('ffi')

-- This test is moved here from the LuaJIT-tests suite since it
-- should be run separately because it exhausts the ctype table.
local test = tap.test('ffi-tabov')

test:plan(3)

-- XXX: Amount of ctypes available to the user of a platform.
-- Was declared in the LuaJIT-tests suite.
local MIN_AVAILABLE_CTYPES = 20000
-- Maximum available amount of ctypes.
local CTID_MAX = 2^16

local last = 0

local res, errmsg = pcall(function()
  for i = 1, CTID_MAX do
    last = i
    ffi.typeof('struct {}')
  end
end)

test:ok(res == false, 'correct status')
test:like(errmsg, 'table overflow', 'correct error message')

test:ok(last > MIN_AVAILABLE_CTYPES, 'huge enough amount of free ctypes')

test:done(true)
