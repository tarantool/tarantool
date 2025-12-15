local tap = require('tap')
local test = tap.test('lj-1024-varg-usedef'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

jit.opt.start('hotloop=1')

local counter = 0
-- luacheck: ignore
local anchor
while counter < 3 do
  counter = counter + 1
  -- BC_VARG 5 1 0. `...` is nil (argument for the script).
  -- We have the following bytecodes to be recorded:
  -- 0031  ADDVN    2   2   0  ; 1
  -- 0032  KSHORT   4   1
  -- 0033  KSHORT   5   1
  -- 0034  ISLE     4   5
  -- 0035  JMP      4 => 0038
  -- 0038  KPRI     4   2
  -- 0039  VARG     5   1   0
  --
  -- 0033 KSHORT bytecode uses the 6th JIT slot and the 5th Lua
  -- slot. This Lua slot will be set to nil after 0039 VARG
  -- bytecode execution, so after VARG recording maxslot should
  -- point to the 5th JIT slot.
  -- luacheck: ignore
  anchor = 1 >= 1, ...
end

test:ok(true, 'BC_VARG recording 0th frame depth')

-- Now the same case, but with an additional frame, so VARG slots
-- are defined on the trace.
local function varg_frame(...)
  -- BC_VARG 1 1 0. `...` is nil (argument for the script).
  -- We have the following bytecodes to be recorded:
  -- 0001  . . KSHORT   0   1
  -- 0002  . . KSHORT   1   1
  -- 0003  . . ISLE     0   1
  -- 0004  . . JMP      0 => 0007
  -- 0007  . . KPRI     0   2
  -- 0008  . . VARG     1   1   0
  --
  -- 0002 KSHORT bytecode uses the 2nd JIT slot and the 1st Lua
  -- slot. This Lua slot will be set to nil after 0008 VARG
  -- bytecode execution, so after VARG recording maxslot should
  -- point to the 1st JIT slot.
  -- luacheck: ignore
  anchor = 1 >= 1, ...
end

counter = 0
while counter < 3 do
  counter = counter + 1
  varg_frame()
end

test:ok(true, 'BC_VARG recording with VARG slots defined on trace')

test:done(true)
