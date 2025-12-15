local tap = require('tap')
local allocinject = require('allocinject')

local test = tap.test('lj-1152-stack-buffer-overflow-on-error')
test:plan(6)

local LJ_MAX_LOCVAR = 200

-- Generate the following Lua chunk:
--   local function recursive_f()
--     local _
--     ...
--     recursive_f()
--   end
--   return recursive_f
local function generate_recursive_f(n_locals)
  local chunk = 'local function recursive_f()\n'
  for _ = 1, n_locals do
    chunk = chunk .. 'local _\n'
  end
  chunk = chunk .. [[
      recursive_f()
    end
    return recursive_f
  ]]
  local f = assert(loadstring(chunk))
  return f()
end

-- Use `coroutine.wrap()` for functions to use newly created stack
-- with fixed number of stack slots.

-- Check that we still got the correct error message in case of
-- the unsafe error handler function.
coroutine.wrap(function()
  -- XXX: Use different recursive functions as callee and handler
  -- to be sure to get the invalid stack value instead of `nil`.
  local function recursive() recursive() end
  local function bad_errfunc() bad_errfunc() end
  local r, e = xpcall(recursive, bad_errfunc)
  -- XXX: Don't create a constant string that is anchored to the
  -- prototype. It is necessary to make the error message freed by
  -- the GC and OOM raising in the last test.
  local EXPECTED_MSG = 'stack ' .. 'overflow'
  test:ok(not r, 'correct status')
  test:like(e, EXPECTED_MSG, 'correct error message')
end)()

coroutine.wrap(function()
  -- Collect all strings including the possibly-existed string
  -- with the 'stack overflow' error message.
  collectgarbage()
  local function recursive() recursive() end
  -- Avoid trace recording. A trace can't be allocated anyway.
  jit.off(recursive)

  -- Check the case when the error
  allocinject.enable_null_alloc()
  local r, e = pcall(recursive)
  allocinject.disable()

  test:ok(not r, 'correct status')
  test:like(e, 'not enough memory', 'correct error message')
end)()

-- Check overflow of the buffer related to the Lua stack.
-- This test fails under ASAN without the patch.
local recursive_f = generate_recursive_f(LJ_MAX_LOCVAR)
coroutine.wrap(function()
  local r, e = pcall(recursive_f)
  test:ok(not r, 'correct status')
  -- XXX: Don't create a constant string that is anchored to the
  -- prototype. It is necessary to make the error message freed by
  -- the GC and OOM raising in the last test.
  local EXPECTED_MSG = 'stack ' .. 'overflow'
  test:like(e, EXPECTED_MSG, 'correct error message')
end)()

test:done(true)
