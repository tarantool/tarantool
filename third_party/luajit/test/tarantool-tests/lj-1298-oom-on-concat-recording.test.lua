local tap = require('tap')

-- Test file to demonstrate unbalanced Lua stack after instruction
-- recording due to throwing an OOM error at the moment of
-- recording without restoring the Lua stack back.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1298.

local test = tap.test('lj-1298-oom-on-concat-recording'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local allocinject = require('allocinject')

test:plan(2)

jit.opt.start('hotloop=1')

-- Allocation limit to return `NULL`.
local ALLOC_LIMIT = 2048

local bigstr = string.rep('x', ALLOC_LIMIT)
local __concat = function()
  return 'concatenated'
end

-- Need to use metamethod call in the concat recording.
-- We may use any object with a metamethod, but let's use a table
-- as the most common one.
local concatable_t = setmetatable({}, {
  __concat = __concat,
})

local function concat_with_err()
  local counter = 0
  local _
  while counter < 1 do
    counter = counter + 1
    _ = bigstr .. concatable_t .. ''
  end
  assert(false, 'unreachable, should raise an error before')
end

-- Returns `NULL` on any allocation beyond the given limit.
allocinject.enable_null_limited_alloc(ALLOC_LIMIT)

local status, errmsg = pcall(concat_with_err)

allocinject.disable()

test:ok(not status, 'no assertion failure during recording, correct status')
test:like(errmsg, 'not enough memory', 'correct error message')

test:done(true)
