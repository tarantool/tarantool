local tap = require('tap')

-- Test file to demonstrate the heap-use-after-free, error for
-- `debug.setmetatable()` and enabled `jit.dump()`.
-- The test fails under ASAN.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1172.

local test = tap.test('lj-1172-debug-handling-ref'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local jdump = require('jit.dump')

test:plan(1)

-- We need to trigger the `TRACE` vmevent handler during
-- `debug.setmetatable()`. It will cause Lua stack reallocation.
jdump.start('t', '/dev/null')

-- Use `coroutine.wrap()` to create a new Lua stack with a minimum
-- number of stack slots.
coroutine.wrap(function()
  -- "TRACE flush" event handler causes stack reallocation and
  -- leads to heap-use-after-free. This event handler is called
  -- because all traces are specialized to base metatables, so
  -- if we update any base metatable, we must flush all traces.
  debug.setmetatable(1, {})
end)()

test:ok(true, 'no heap-use-after-free error')

test:done(true)
