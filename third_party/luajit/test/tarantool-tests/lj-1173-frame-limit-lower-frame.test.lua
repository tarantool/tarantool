local tap = require('tap')

-- Test file to demonstrate LuaJIT assertion failure during
-- recording of side trace in GC64 mode with return to lower
-- frame, which has the maximum possible frame size.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1173.

local test = tap.test('lj-1173-frame-limit-lower-frame'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- Parent trace with stitching and returning to a lower frame.
local function retf()
  math.modf(1)
end
_G.retf = retf

-- The maximum number of stack slots for a trace. Defined in the
-- <src/lj_def.h>. Also, it equals `LJ_MAX_SLOTS` -- the maximum
-- number of slots in a Lua function.
local LJ_MAX_JSLOTS = 250

-- Generate the following function:
-- | local uv = {key = 1}
-- | return function()
-- |   local r = retf()
-- |   uv.key, uv.key, --[[124 times in total ...]] uv.key = nil
-- | end
-- It will have the following bytecode:
-- | 0001    GGET     0   0      ; "retf"
-- | 0002    CALL     0   2   1
-- | 0003    UGET     1   0      ; uv
-- | ...
-- | 0126    UGET   124   0      ; uv
-- | 0127    KNIL   125 248
-- | 0128    TSETS  248 124   1  ; "key"
-- | ...
-- | 0251    TSETS  125   1   1  ; "key"
-- | 0252    RET0     0   1
-- As you can see, the 249 slots (from 0 to 248) are occupied in
-- total.
-- When we return to the lower frame for the side trace, we may
-- hit the slot limit mentioned above: 2 slots are occupied
-- by the frame (`baseslot`) and `KNIL` bytecode recording sets
-- `maxslot` (the first free slot) to 249. Hence, the JIT slots
-- are overflowing.

local chunk = [[
local uv = {key = 1}
return function()
  local r = retf()
]]

-- Each `UGET` occupies 1 slot, `KNIL` occupies the same amount.
-- 1 slot is reserved (`r` variable), 1 pair is set outside the
-- cycle. 249 slots (the maximum available amount, see
-- <src/lj_parse.c>, `bcreg_bump()` for details) are occupied in
-- total.
for _ = 1, LJ_MAX_JSLOTS / 2 - 2 do
  chunk = chunk .. ('uv.key, ')
end
chunk = chunk .. [[uv.key = nil
end]]

local get_func = assert(loadstring(chunk))
local function_max_framesize = get_func()

jit.opt.start('hotloop=1', 'hotexit=1')

-- Compile the parent trace first.
retf()
retf()

-- Try to compile the side trace with a return to a lower frame
-- with a huge frame size.
function_max_framesize()
function_max_framesize()

-- XXX: The limit check is OK with default defines for non-GC64
-- mode, the trace is compiled for it. The test fails only with
-- GC64 mode enabled. Still run the test for non-GC64 mode to
-- avoid regressions.

test:ok(true, 'no assertion failure during recording')

test:done(true)
