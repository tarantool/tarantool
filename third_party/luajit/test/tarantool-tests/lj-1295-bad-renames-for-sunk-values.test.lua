local tap = require('tap')

-- Test file to demonstrate LuaJIT assembler misbehaviour.
-- For more info, proceed to the issues:
-- * https://github.com/LuaJIT/LuaJIT/issues/584,
-- * https://github.com/LuaJIT/LuaJIT/issues/1295,
-- * https://github.com/tarantool/tarantool/issues/10746.

local test = tap.test('lj-1295-bad-renames-for-sunk-values'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- XXX: The reproducer requires specific snapshotting and register
-- allocations, so the reproducer mostly copies the relevant code
-- from https://github.com/luafun/luafun.

----- Related part of luafun.lua. --------------------------------

local iterator_mt = {
  -- Usually called by the for-in loop.
  __call = function(self, param, state)
    return self.gen(param, state)
  end,
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
local chain_gen_r1
local chain_gen_r2 = function(param, state, state_x, ...)
  if state_x == nil then
    local i = state[1]
    i = i + 1
    if param[3 * i - 2] == nil then
      return nil
    end
    return chain_gen_r1(param, {i, param[3 * i]})
  end
  return {state[1], state_x}, ...
end

chain_gen_r1 = function(param, state)
  local i, _ = state[1], state[2]
  local gen_x, param_x = param[3 * i - 2], param[3 * i - 1]
  return chain_gen_r2(param, state, gen_x(param_x, state[2]))
end

local chain = function(...)
  local n = select('#', ...)
  local param = { [3 * n] = 0 }
  local gen_x, param_x, state_x
  for i = 1, n do
    local elem = select(i, ...)
    -- Put gen, param, state into param table.
    gen_x, param_x, state_x = wrap(ipairs(elem))
    param[3 * i - 2] = gen_x
    param[3 * i - 1] = param_x
    param[3 * i] = state_x
  end

  return wrap(chain_gen_r1, param, {1, param[3]})
end

----- Reproducer. ------------------------------------------------

-- XXX: Should be different tables.
local a = {{'a'}, {'a'}}
local b = {{'a'}, {'a'}}

-- XXX: Here one can find the rationale for the 'hotloop' value.
-- 1. The innermost loop in a bunch of calls tries to compile the
-- `__call` metamethod first, but this trace is aborted due to
-- leaving the `for _ in` loop.
-- 2. The trace we are interested in started to compile: this is
-- the inner `for _ in` loop with the full inlined body.
--
-- The chain loop in this case iterates over 4 elements. Hence, 2
-- iterations of the outer loop are not enough. The trace
-- mentioned in 2 is recorded on the 8th inner cycle iteration but
-- never started. Hence, let's run 3 iterations of the outer loop
-- instead.

-- JIT fine-tuning via the 'hotloop' option allows us to catch
-- this elusive case, which we achieved in the last bullet. The
-- reason why this case leads to misbehaviour while restoring the
-- guest stack at the trace exit is described in the following
-- LuaJIT issue: https://github.com/LuaJIT/LuaJIT/issues/1295.

-- Specific `hotloop` to get only one trace we are interested in.
jit.opt.start('hotloop=7')

xpcall(function()
  for _ = 1, 3 do
    for _ in chain(a, b) do
    end
  end
  test:ok('All emitted RENAMEs are fine')
end, function()
  test:fail('Invalid Lua stack has been restored')
end)

test:done(true)
