local tap = require('tap')

-- Test file to demonstrate LuaJIT's incorrect fold optimization
-- for Array Bound Check for constants.
-- ABC(asize, k1), ABC(asize k2) ==> ABC(asize, max(k1, k2)).
-- See also https://github.com/LuaJIT/LuaJIT/issues/794.

local test = tap.test('lj-794-abc-fold-constants'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

local MAGIC_UNUSED = 42

local function abc_check_sign()
  local tab = {MAGIC_UNUSED}
  local return_value = 0
  local abc_limit = 1
  -- No need to run the loop on the first call. We will take
  -- the side exit anyway.
  for i = 1, 3 do
    -- Add an additional ABC check to be merged with.
    if i > 1 then
      -- luacheck: ignore
      return_value = tab[1]
      return_value = tab[abc_limit]
      -- XXX: Just use some negative number.
      abc_limit = -1000000
    end
  end
  return return_value
end

jit.opt.start('hotloop=1')

-- Compile the loop.
abc_check_sign()
-- Run the loop.
test:is(abc_check_sign(), nil, 'correct ABC constant rule for negative values')

-- Now test the second issue, when ABC optimization applies for
-- operands across PHIs.

-- XXX: Reset hotcounters to avoid collisions.
jit.opt.start('hotloop=1')

local tab_array = {}
local small_tab = {MAGIC_UNUSED}
local full_tab = {}

-- First, create tables with different asizes, to be used in PHI.
-- Create a large enough array part for the noticeable
-- out-of-bounds access.
for i = 1, 8 do
  full_tab[i] = MAGIC_UNUSED
end

-- Now, store these tables in the array. The PHI should be used in
-- the trace to distinguish asizes from the variant and the
-- invariant parts of the loop for the future ABC check.
-- Nevertheless, before the patch, the ABC IR and the
-- corresponding PHI are folded via optimization. This leads to
-- incorrect behaviour.
-- We need 5 iterations to execute both the variant and the
-- invariant parts of the trace below.
for i = 1, 5 do
  -- On the 3rd iteration, the recording is started.
  if i > 3 then
    tab_array[i] = small_tab
  else
    tab_array[i] = full_tab
  end
end

local result
local alias_tab = tab_array[1]
-- Compile a trace.
-- Run 5 iterations to execute both the variant and the invariant
-- parts.
for i = 1, 5 do
  local previous_tab = alias_tab
  alias_tab = tab_array[i]
  -- Additional ABC check to fold.
  -- luacheck: ignore
  result = alias_tab[1]
  result = previous_tab[8]
end

test:is(result, nil, 'correct ABC constant rule across PHI')

test:done(true)
