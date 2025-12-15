local tap = require('tap')
-- Test file to demonstrate the incorrect JIT assembling of unused
-- `IR_SLOAD` with number type on arm64.
-- See also https://github.com/LuaJIT/LuaJIT/issues/903.
local test = tap.test('lj-903-arm64-unused-number-sload-typecheck'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- Just use any different numbers (but not integers to avoid
-- integer IR type).
local SLOT = 0.1
local MARKER_VALUE = 4.2
-- XXX: Special mapping to avoid folding and removing always true
-- comparison.
local anchor = {marker = MARKER_VALUE}

-- Special function to inline on trace to generate SLOAD
-- typecheck.
local function sload_unused(x)
  return x
end

-- The additional wrapper to use stackslots in the function.
local function test_sload()
  local sload = SLOT
  for _ = 1, 4 do
    -- This line should use the `d0` register.
    local marker = anchor.marker - MARKER_VALUE
    -- This generates unused IR_SLOAD with typecheck (number).
    -- Before the patch, it occasionally overwrites the `d0`
    -- register and causes the execution of the branch.
    sload_unused(sload)
    if marker ~= 0 then
      return false
    end
  end
  return true
end

jit.opt.start('hotloop=1')
test:ok(test_sload(), 'correct SLOAD assembling')

test:done(true)
