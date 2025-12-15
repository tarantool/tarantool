local tap = require('tap')

-- Test file to demonstrate LuaJIT's crash in cases of sunk
-- restore for huge tables.
-- See also https://github.com/LuaJIT/LuaJIT/issues/1132.

local test = tap.test('lj-1132-bad-snap-refs'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local table_new = require('table.new')

jit.opt.start('hotloop=1', 'hotexit=1')

local result_tab
-- Create a trace containing the IR instruction:
-- | {sink}  tab TNEW   #32762  #0
-- `lj_snap_replay()` assumes that 32762 (0x7ffa) (op1 of TNEW) is
-- a constant reference. It is passed to the `snap_replay_const()`
-- lookup to the IR constant in the 0x7ffa slot.
-- This slot contains the second part of the IR constant
-- number 0.5029296875 (step of the cycle) in its raw form
-- (0x3fe0180000000000). The 0x18 part is treated as IROp
-- (IR_KGC), and JIT is trying to continue with a store of an
-- invalid GC object, which leads to a crash.
for i = 1, 2.5, 0.5029296875 do
  local sunk_tab = table_new(0x7ff9, 0)
  -- Force the side exit with restoration of the sunk table.
  if i > 2 then result_tab = sunk_tab end
end

test:ok(type(result_tab) == 'table', 'no crash during sunk restore')

test:done(true)
