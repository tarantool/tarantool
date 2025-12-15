local tap = require('tap')

-- The test file to demonstrate UBSan warning for `setfenv()` and
-- `getfenv()` with a huge `level` value.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1329.
local test = tap.test('lj-1329-getfenv-setfenv-negative')

test:plan(4)

-- This number will be equal to `INT_MIN` when casted to `int`.
-- After this, it will be decremented in `lj_debug_level()` and
-- underflowed to the `INT_MAX`. That produces the UBSan warning
-- about signed integer overflow.
local LEVEL = 2 ^ 31
local ERRMSG = 'invalid level'

-- Tests check the UBSan runtime error. Add assertions just to be
-- sure that we don't change the behaviour.
local status, errmsg = pcall(getfenv, LEVEL)
test:ok(not status, 'getfenv: correct status')
test:like(errmsg, ERRMSG, 'getfenv: correct error message')

status, errmsg = pcall(setfenv, LEVEL, {})
test:ok(not status, 'setfenv: correct status')
test:like(errmsg, ERRMSG, 'setfenv: correct error message')

test:done(true)
