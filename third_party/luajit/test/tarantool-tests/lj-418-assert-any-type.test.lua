local tap = require('tap')

-- See also https://github.com/LuaJIT/LuaJIT/issues/418.
local test = tap.test('lj-418-asset-any-type')
test:plan(2)

local retv = {}

local st, err = pcall(assert, false, retv)
assert(not st, 'pcall fails')

test:ok(err == retv, 'assert function take non-string argument')

xpcall(assert, function(obj)
  test:ok(obj == retv, 'xpcall error handler function get non-string argument')
end, false, retv)

test:done(true)
