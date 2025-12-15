local tap = require('tap')
local gcisblack = require('utils').gc.isblack

local test = tap.test('fix-gc-setupvalue')
test:plan(1)

-- Test file to demonstrate LuaJIT GC invariant violation
-- for inherited upvalues.

-- The bug is about the situation, when black upvalue refers to
-- a white object. This happens when the parent function is marked
-- first (all closed upvalues and function are colored to black),
-- and then `debug.setupvalue()` is called for a child function
-- with inherited upvalues. The barrier is moved forward for a
-- non-marked function (instead upvalue) and invariant is
-- violated as a result.

-- Create two functions with closed upvalue.
do
  local uv = 1
  local function parent()
    local function child()
      return uv + 1
    end
    _G.child = child
    return uv + 1
  end
  -- Set up `child()`.
  parent()
  _G.parent = parent
end

-- Set GC on start.
collectgarbage()

-- Set minimally possible stepmul.
-- 1024/10 * stepmul == 10 < sizeof(GCfuncL), so it guarantees,
-- that 2 functions will be marked in different time.
local oldstepmul = collectgarbage('setstepmul', 1)

-- `parent()` function is marked before `child()`, so wait until
-- it becomes black and proceed with the test.
while not gcisblack(_G.parent) do
  collectgarbage('step')
end

-- Set created string (white) for the upvalue.
debug.setupvalue(_G.child, 1, '4'..'1')
_G.child = nil

-- Lets finish it faster.
collectgarbage('setstepmul', oldstepmul)
-- Finish GC cycle to be sure that the object is collected.
while not collectgarbage('step') do end

-- Generate some garbage to reuse freed memory.
for i = 1, 1e2 do local _ = {string.rep('0', i)} end

test:ok(_G.parent() == 42, 'correct set up of upvalue')

test:done(true)
