local tap = require('tap')
-- Test file to demonstrate LuaJIT misbehaviour during recording
-- BC_VARG with nvarargs >= nresults in GC64 mode.
-- See also https://github.com/LuaJIT/LuaJIT/issues/864,
-- https://github.com/tarantool/tarantool/issues/7172.
local test = tap.test('lj-864-varg-rec-base-offset'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

jit.opt.start('hotloop=1')

local function test_rec_varg(...)
  local trace_value, interp_value
  for _ = 1, 3 do
    trace_value = ...
  end
  interp_value = ...
  return trace_value == interp_value
end

-- Test case for nvarargs >= nresults. Equality is not suitable
-- due to failing assertion guard for type of loaded vararg slot.
test:ok(test_rec_varg(42, 0), 'correct BC_VARG recording')

test:done(true)
