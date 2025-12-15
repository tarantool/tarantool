local tap = require('tap')

-- Test file to demonstrate incorrect behaviour of cdata
-- concatenation in LuaJIT.
-- See also
-- https://www.freelists.org/post/luajit/cdata-concatenation.
local test = tap.test('cdata-concat')
test:plan(2)

local r, e = pcall(function()
  return 1LL .. 2LL
end)
test:ok(not r and e:match('attempt to concatenate'), 'cdata concatenation')

-- Check, that concatenation work, when metamethod is defined.
debug.getmetatable(1LL).__concat = function(a, b)
  return tostring(a) .. tostring(b)
end
test:ok(1LL .. 2LL == '1LL2LL', 'cdata concatenation with defined metamethod')

test:done(true)
