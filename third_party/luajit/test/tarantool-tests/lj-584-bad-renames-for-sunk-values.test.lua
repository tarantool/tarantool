local tap = require('tap')
local test = tap.test('lj-584-bad-renames-for-sunk-values'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- Test file to demonstrate LuaJIT assembler misbehaviour.
-- For more info, proceed to the issues:
-- * https://github.com/LuaJIT/LuaJIT/issues/584
-- * https://github.com/tarantool/tarantool/issues/4252

----- Related part of luafun.lua. --------------------------------

local iterator_mt = {
  __call = function(self, param, state) return self.gen(param, state) end,
}

local wrap = function(gen, param, state)
  return setmetatable({
    gen = gen,
    param = param,
    state = state
  }, iterator_mt), param, state
end

-- These functions call each other to implement a flat iterator
-- over the several iterable objects.
local chain_gen_r1, chain_gen_r2

chain_gen_r2 = function(param, state, state_x, ...)
  if state_x ~= nil then return { state[1], state_x }, ...  end
  local i = state[1] + 1
  if param[3 * i - 1] == nil then return nil end
  return chain_gen_r1(param, { i, param[3 * i] })
end

chain_gen_r1 = function(param, state)
  local i, state_x = state[1], state[2]
  local gen_x, param_x = param[3 * i - 2], param[3 * i - 1]
  return chain_gen_r2(param, state, gen_x(param_x, state_x))
end

local chain = function(...)
  local param = { }
  for i = 1, select('#', ...) do
    -- Put gen, param, state into param table.
    param[3 * i - 2], param[3 * i - 1], param[3 * i]
      = wrap(ipairs(select(i, ...)))
  end
  return wrap(chain_gen_r1, param, { 1, param[3] })
end

----- Reproducer. ------------------------------------------------

-- XXX: Here one can find the rationale for the 'hotloop' value.
-- 1. The most inner while loop on the line 86 starts recording
--    for the third element (i.e. 'c') and successfully compiles
--    as TRACE 1. However, its execution stops, since type guard
--    for <gen_x> result value on line 39 is violated (nil is
--    returned from <ipairs_aux>) and trace execution is stopped.
-- 2. Next time TRACE 1 enters the field is iterating through the
--    second table given to <chain>. Its execution also stops at
--    the similar assertion but in the variant part this time.
-- 3. <wrap> function becomes hot enough while building new
--    <chain> iterator, and it is compiled as TRACE 2.
--    There are also other attempts, but all of them failed.
-- 4. Again, TRACE 1 reigns while iterating through the first
--    table given to <chain> and finishes at the same guard the
--    previous run does. Anyway, everything above is just an
--    auxiliary activity preparing the JIT environment for the
--    following result.
-- 5. Here we finally come: <chain_gen_r1> is finally ready to be
--    recorded. It successfully compiles as TRACE 3. However, the
--    boundary case is recorded, so the trace execution stops
--    since nil *is not* returned from <ipairs_aux> on the next
--    iteration.
--
-- JIT fine tuning via 'hotloop' option allows to catch this
-- elusive case, we achieved in a last bullet. The reason, why
-- this case leads to a misbehaviour while restoring the guest
-- stack at the trace exit, is described in the following LuaJIT
-- issue: https://github.com/LuaJIT/LuaJIT/issues/584.
jit.opt.start('hotloop=3')

xpcall(function()
  for _ = 1, 3 do
    local gen_x, param_x, state_x = chain({ 'a', 'b', 'c' }, { 'q', 'w', 'e' })
    while true do
      state_x = gen_x(param_x, state_x)
      if state_x == nil then break end
    end
  end
  test:ok('All emitted RENAMEs are fine')
end, function()
  test:fail('Invalid Lua stack has been restored')
end)

test:done(true)
