local tap = require('tap')

-- Test file to demonstrate LuaJIT dirty reads after stack
-- overflow during restoration from the snapshot.
-- The test fails before the patch under Valgrind.
--
-- luacheck: push no max_comment_line_length
--
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1196,
-- https://www.freelists.org/post/luajit/Invalid-read-found-by-valgrind.
--
-- luacheck: pop

local test = tap.test('lj-1196-stack-overflow-snap-restore')

test:plan(4)

-- XXX: This file has the same tests as the
-- <test/LuaJIT-tests/lang/stackov.lua>, but without disabling the
-- compilation for the given functions. Hence, the check here is
-- less strict -- we just check that there are no dirty reads,
-- uninitialized memory access, etc.

local function recursive_f_noarg()
  recursive_f_noarg()
end

local function recursive_one_arg(argument)
  recursive_one_arg(argument)
end

local function recursive_f_vararg(...)
  recursive_f_vararg(1, ...)
end

local function recursive_f_vararg_tail(...)
  return recursive_f_vararg_tail(1, ...)
end

-- Use `coroutine.wrap()`, for independent stack sizes.
-- The invalid read is done by the error handler
-- `debug.traceback()`, since it observes the pseudo PC (`L`) and
-- reads the memory by `L - 4` address before the patch.

coroutine.wrap(function()
  local status = xpcall(recursive_f_noarg, debug.traceback)
  test:ok(not status, 'correct status, recursive no arguments')
end)()

coroutine.wrap(function()
  local status = xpcall(recursive_one_arg, debug.traceback, 1)
  test:ok(not status, 'correct status, recursive one argument')
end)()

coroutine.wrap(function()
  local status = xpcall(recursive_f_vararg, debug.traceback, 1)
  test:ok(not status, 'correct status, recursive vararg')
end)()

coroutine.wrap(function()
  local status = xpcall(recursive_f_vararg_tail, debug.traceback, 1)
  test:ok(not status, 'correct status, recursive vararg tail')
end)()

test:done(true)
