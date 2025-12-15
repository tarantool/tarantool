local tap = require('tap')

local test = tap.test('lj-426-arm64-incorrect-check-closed-uv')
test:plan(1)

-- Test file to demonstrate LuaJIT USETS bytecode incorrect
-- behaviour on arm64 in case when non-white object is set to
-- closed upvalue.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/426.

-- First, create a closed upvalue.
do
  local uv -- luacheck: no unused
  -- The function's prototype is created with the following
  -- constants at chunk parsing. After adding this constant to
  -- the function's prototype it will be marked as gray during
  -- propagate phase.
  local function usets() uv = '' end
  _G.usets = usets
end

-- Set GC state to GCpause.
collectgarbage()

-- We want to wait for the situation, when upvalue is black,
-- the string is gray. Both conditions are satisfied, when the
-- corresponding `usets()` function is marked, for example.
-- We don't know on what exactly step our upvalue is marked as
-- black and USETS become dangerous, so just check it at each
-- step.
-- Don't need to do the full GC cycle step by step.
local old_steps_atomic = misc.getmetrics().gc_steps_atomic
while (misc.getmetrics().gc_steps_atomic == old_steps_atomic) do
  collectgarbage('step')
  usets() -- luacheck: no global
end

test:ok(true)
test:done(true)
