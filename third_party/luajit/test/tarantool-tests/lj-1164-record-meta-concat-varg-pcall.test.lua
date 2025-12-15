local tap = require('tap')

-- Test file to demonstrate the incorrect recording of the
-- `__concat` metamethod with the VARG or protected frame for the
-- function used in the `__concat`, when we have to record more
-- than 2 objects.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1164.

local test = tap.test('lj-1164-record-meta-concat-varg-pcall'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

jit.opt.start('hotloop=1')

-- Table with the simplest varg metamethod to call.
local tab_varg = setmetatable({}, {
  __concat = function(...)
    local _, tab_obj = ...
    return tab_obj
  end,
})

local result
for _ = 1, 4 do
  -- The concatenation has right to left associativity.
  result = '' .. '' .. tab_varg
end

test:is(result, tab_varg, 'varg frame is recorded correctly')

-- Table with the simplest protected metamethod.
local tab_pcall = setmetatable({}, {
  -- Need to use `__call` metamethod to make the object callable.
  __call = function(tab_obj)
    return tab_obj
  end,
  __concat = pcall,
})

for _ = 1, 4 do
  -- The concatenation has right to left associativity.
  result = tab_pcall .. tab_pcall .. ''
end

test:is(result, true, 'protected frame is recorded correctly')

test:done(true)
