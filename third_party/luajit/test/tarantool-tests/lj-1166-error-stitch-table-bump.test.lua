local tap = require('tap')

-- Test file to demonstrate unbalanced Lua stack after instruction
-- recording due to throwing an error at recording of a stitched
-- function. The test fails with LUAJIT_ENABLE_TABLE_BUMP enabled.
-- See also:
-- * https://github.com/LuaJIT/LuaJIT/issues/606,
-- * https://github.com/LuaJIT/LuaJIT/issues/1166.

local test = tap.test('lj-1166-error-stitch-table-bump'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- `math.modf` recording is NYI.
-- Local `modf` simplifies `jit.dump()` output.
local modf = math.modf

jit.opt.start('hotloop=1')

-- luacheck: no unused
local t
-- There is no need to run the trace itself. Just check the
-- correctness of a recording.
for i = 1, 2 do
  t = {}
  -- Cause table rehashing to trigger table bump optimization.
  t[i] = i
  -- Forcify stitch. This will throw an error at the end of
  -- recording, since trace recording should be retried after
  -- bytecode updating.
  modf(1)
end

test:ok(true, 'stack is balanced')

test:done(true)
