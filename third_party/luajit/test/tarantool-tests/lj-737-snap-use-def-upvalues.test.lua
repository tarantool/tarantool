local tap = require('tap')

-- Test file to demonstrate LuaJIT misbehaviour in use-def
-- snapshot analysis for local upvalues.
-- See also https://github.com/LuaJIT/LuaJIT/issues/737.

local test = tap.test('lj-737-snap-use-def-upvalues'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- XXX: simplify `jit.dump()` output.
local fmod = math.fmod

local EXPECTED = 'expected'

jit.opt.start('hotloop=1')

local function wrapped_trace(create_closure)
  local local_upvalue, closure
  if create_closure then
    closure = function() return local_upvalue end
  end
  for i = 1, 4 do
    -- On the second iteration, the trace is recorded.
    if i == 2 then
      -- Before the patch, this slot was considered unused by
      -- use-def analysis in the `snap_usedef()` since there are
      -- no open unpvalues for `closure()` on recording
      -- (1st call).
      local_upvalue = EXPECTED
      -- luacheck: ignore
      -- Emit an additional snapshot after setting the
      -- upvalue.
      if i == 0 then end
      -- Stitching ends the trace here.
      fmod(1,1)
      return closure
    end
  end
end

-- Compile the trace.
local func_with_uv = wrapped_trace(false)
assert(func_with_uv == nil, 'no function is returned on the first call')

-- Now run this trace when `closure()` is defined and has an open
-- local upvalue.
func_with_uv = wrapped_trace(true)
assert(type(func_with_uv) == 'function',
       'function is returned after the second call')

test:is(func_with_uv(), EXPECTED, 'correct result of the closure call')

test:done(true)
