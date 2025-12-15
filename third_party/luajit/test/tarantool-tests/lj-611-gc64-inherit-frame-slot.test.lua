local tap = require('tap')
local test = tap.test('lj-611-gc64-inherit-frame-slot'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

-- GC64: Function missing in snapshot for non-base frame.
-- The reproducer is generated from the fuzzer.
-- https://github.com/LuaJIT/LuaJIT/issues/611.

test:plan(1)

jit.opt.start('hotloop=1', 'hotexit=1')

local inner_counter = 0
local SIDE_START = 1
-- Lower frame to return from `inner()` function side trace.
-- XXX: Use a vararg frame to prevent compilation of the function.
-- The FNEW bytecode is NIY for now, so this helps to avoid trace
-- blacklisting.
local function lower_frame(...)
  local inner = function()
    if inner_counter > SIDE_START then
      return
    end
    inner_counter = inner_counter + 1
  end
  -- XXX: We need to return to the lower frame (to the same
  -- function) several times to produce the necessary side traces.
  -- See `jit.dump()` for the details.
  inner(..., inner(inner()))
end

-- Compile `inner()` function.
-- XXX: We need at least 3 calls to create several function
-- objects from the prototype of the `inner()` function and hit
-- the `PROTO_CLC_POLY` limit, so the side traces stop spawning.
-- See also:
-- luacheck: push no max_comment_line_length
-- https://github.com/tarantool/tarantool/wiki/LuaJIT-function-inlining.
-- luacheck: pop
lower_frame()
lower_frame()
-- Compile hotexit.
lower_frame()
-- Take side exit from side trace.
lower_frame(1)

test:ok(true, 'function is present in the snapshot')
test:done(true)
