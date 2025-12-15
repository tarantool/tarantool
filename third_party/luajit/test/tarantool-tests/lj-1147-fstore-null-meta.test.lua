local tap = require('tap')

-- Test file to demonstrate LuaJIT's incorrect restoration of a
-- table from a snapshot when the `setmetatable()` gets `nil` as
-- the second argument.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1147.

local test = tap.test('lj-1147-fstore-null-meta'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

jit.opt.start('hotloop=1')

local counter = 0
local tab
-- XXX: The loop is limited to 3 iterations to compile a trace and
-- start to execute it. Use the loop format to see
-- the side effects on the restoration from the snapshot.
local LOOP_LIMIT = 2
while true do
  counter = counter + 1
  -- Use counter for the content check.
  tab = {counter}
  -- This emits the following IRs necessary for the assertion
  -- failure:
  -- | 0003 }+ tab TNEW   #3    #0
  -- | ...
  -- | 0015    p64 FREF   0003  tab.meta
  -- | 0016 }  tab FSTORE 0015  NULL
  setmetatable(tab, nil)
  -- Emit exit here to be sure that the table will be restored
  -- from the snapshot.
  if counter > LOOP_LIMIT then break end
end

test:is(tab[1], LOOP_LIMIT + 1, 'correct table content')
test:ok(debug.getmetatable(tab) == nil, 'no metatable on the restored table')

test:done(true)
