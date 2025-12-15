local tap = require('tap')
local test = tap.test('fix-jit-dump-ir-conv'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

test:plan(2)

-- Test file to demonstrate LuaJIT incorrect `jit.dump()` output
-- for `IR_CONV`.

local jparse = require('utils').jit.parse

-- XXX: Avoid any traces compilation due to hotcount collisions
-- for predictable results.
jit.off()
jit.flush()

jit.opt.start('hotloop=1')

jit.on()
jparse.start('i')

-- luacheck: ignore
local tab = {}
local idx = 1
for _ = 1, 4 do
  -- `int IR_CONV int.num index`.
  tab[idx] = idx
end

local traces = jparse.finish()

-- Skip tests for DUALNUM mode since it has no conversions (for
-- the same cases).
local IS_DUALNUM = not traces[1]:has_ir('num SLOAD')

test:ok(IS_DUALNUM or traces[1]:has_ir('CONV.*int.num index'),
        'correct dump for index')

local function trace(step)
  -- `int IR_CONV int.num check` for `step` conversion.
  for _ = 1, 4, step do
  end
end

-- XXX: Reset hotcounters and traces. Use `hotloop=2` to avoid
-- penalty for the outer trace.
jit.flush()
jit.opt.start('hotloop=2')

jparse.start('i')
-- Compile the inner trace first.
trace(1)
trace(1)
-- Compile the big trace with the first trace inlined.
-- Needs narrowing optimization enabled.
-- XXX: Reset hotcounters. Needed to avoid hotcount collisions.
jit.opt.start('hotloop=1')
trace(1)
trace(1)

traces = jparse.finish()

test:ok(IS_DUALNUM or traces[2]:has_ir('CONV.*int.num check'),
        'correct dump for check')

test:done(true)
