local tap = require('tap')

-- Test file to demonstrate next FF incorrect behaviour on LJ_64.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/727.

local test = tap.test('lj-727-lightuserdata-itern')
test:plan(1)

local ud = require('lightuserdata').craft_ptr_wp()

-- Before the patch we have the tagged lightuserdata pointer
-- 0xFFFE7FFF00000002 in `ud` equal to the ITERN control variable
-- after the first iteration.
-- This control variable denotes the current index in the table
-- in loops with `next()` builtin. If the ITERN is then
-- despecialized to ITERC, the full value of the control variable
-- (i.e. magic + index) succeeds the lookup, so some elements can
-- be skipped during such misiteration.

local real_next = next
local next = next

local function itern_test(t, f)
  for k, v in next, t do
    f(k, v)
  end
end

local visited = {}
local t = {1, 2, 3, [ud] = 45}
itern_test(t, function(k, v)
  visited[k] = v
  if next == real_next then
    next = function(tab, key)
      return real_next(tab, key)
    end
    -- Despecialize bytecode.
    itern_test({}, function() end)
  end
end)

test.strict = true
test:is_deeply(visited, t, 'userdata node is visited')

test:done(true)
