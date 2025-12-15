local tap = require('tap')
local test = tap.test('unit-jit-parse'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

local jparse = require('utils').jit.parse

-- XXX: Avoid any other traces compilation due to hotcount
-- collisions for predictable results.
jit.off()
jit.flush()

local expected_irs = {
  -- The different exotic builds may add different IR
  -- instructions, so just check some IR-s existence.
  -- `%d` is a workaround for GC64 | non-GC64 stack slot number.
  'int SLOAD  #%d',
  'int ADD    0001  %+1',
}
local N_TESTS = #expected_irs

test:plan(N_TESTS)

jit.on()
jparse.start('i')

jit.opt.start('hotloop=1')

-- Loop to compile:
for _ = 1, 3 do end

local traces = jparse.finish()

jit.off()

local loop_trace = traces[1]

for irnum = 1, N_TESTS do
  local ir_pattern = expected_irs[irnum]
  local irref = loop_trace:has_ir(ir_pattern)
  test:ok(irref, 'find IR reference by pattern: ' .. ir_pattern)
end

test:done(true)
