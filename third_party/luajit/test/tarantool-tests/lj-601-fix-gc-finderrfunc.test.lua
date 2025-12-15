local tap = require('tap')

local mixcframe = require('libmixcframe')
local test = tap.test('lj-601-fix-gc-finderrfunc')
test:plan(1)

-- Test file to demonstrate LuaJIT incorrect behaviour, when
-- throwing error in __gc finalizer.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/601.

-- Stop GC for now.
collectgarbage('stop')

local a = newproxy(true)
getmetatable(a).__gc = function()
  -- Function to raise error via `lj_err_run()` inside __gc.
  error('raise error in __gc')
end
-- luacheck: no unused
a = nil

-- We need to get the following Lua stack format when raise an
-- error:
-- + L->stack
-- | ...
-- | CP -- any C protected frame.
-- | ...[L/LP/V]...
-- | C -- any C frame.
-- | ...[L/LP/V]...
-- | CP (with inherited errfunc) -- __gc frame.
-- V
-- Enter in the C land to call a function in a protected C frame
-- (CP). Spoil host stack (and ergo cframe area) and later call
-- Lua C function, triggering full GC cycle in a non-protected
-- frame. As a result, error is raised in __gc metamethod above.
test:ok(mixcframe.test_handle_err(), 'error in __gc is successfully handled')

test:done(true)
