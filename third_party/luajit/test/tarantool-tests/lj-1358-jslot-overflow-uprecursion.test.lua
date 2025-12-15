local tap = require('tap')

-- The test file to demonstrate JIT slots overflow when compiling
-- the return from the trace with up-recursion.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1358.

local test = tap.test('lj-1358-jslot-overflow-uprecursion'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- The test generates the functions with the following workload:
--
-- | local uprec_func()
-- |   if cond then return end
-- |   return 'x', --[[...]] 'x', uprec_func()
-- | end
-- |
-- | local function empty() end
-- | empty('x', --[[...]] 'x', uprec_func())
--
-- The recording of the return from `uprec_func()` before the call
-- to `empty()` causes the assertion failure in the
-- `rec_check_slots()`.

-- Generate a function with many return values plus up-recursion.
local function generate_uprec_payload(n_returns)
  local str_func = [[
  local counter = 0
  local function payload_f()
    counter = counter + 1
    if counter > 5 then return end
    return
  ]]
  for _ = 1, n_returns do
    str_func = str_func .. '"x", '
  end
  str_func = str_func .. [[
    payload_f()
  end
  return payload_f
  ]]
  local f = assert(loadstring(str_func))
  return f()
end

-- Generate the necessary number of locals for a huge enough
-- `cbase`.
local function generate_nloc_payload(n_locals)
  local str_func = [[
  -- Function to be called after return with all stack slots used.
  local function empty() end
  empty(
  ]]
  for _ = 1, n_locals do
    str_func = str_func .. '"x", '
  end
  str_func = str_func .. [[
    _G.uprec_func()
  )
  ]]
  local f = assert(loadstring(str_func))
  return f
end

-- Avoid an unrelated JIT output.
jit.off()
-- 30 * 5 = 150 returned values for the first call.
_G.uprec_func = generate_uprec_payload(30)
-- Plus 100 slots for locals, plus a slot for the function to be
-- called causes JIT stack slots overflow.
local test_func = generate_nloc_payload(100)

jit.on()
jit.opt.start('hotloop=1', 'hotexit=1', 'recunroll=1')

test_func()

test:ok(true, 'no assertion on JIT slots overflow')

test:done(true)
