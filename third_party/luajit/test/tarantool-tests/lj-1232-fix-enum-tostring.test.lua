local tap = require('tap')

-- Test file to demonstrate heap-buffer-overflow in the
-- `tostring()` call for the enum cdata.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1232.

local test = tap.test('lj-1232-fix-enum-tostring')

local ffi = require('ffi')
local ENUM_VAL = 1
local EXPECTED = 'cdata<enum %d+>: ' .. ENUM_VAL

test:plan(1)

local cdata_enum = ffi.new(('enum {foo = %d}'):format(ENUM_VAL), ENUM_VAL)

-- XXX: The test shows heap-buffer-overflow only under ASAN.

test:like(tostring(cdata_enum), EXPECTED, 'correct tostring result')

test:done(true)
