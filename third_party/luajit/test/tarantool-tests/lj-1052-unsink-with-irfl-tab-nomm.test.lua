local tap = require('tap')

-- Test file to demonstrate LuaJIT's incorrect restoration of a
-- table from a snapshot with the presence of `IRFL_TAB_NOMM`.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1052.

local test = tap.test('lj-1052-unsink-with-irfl-tab-nomm'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

local TEST_VALUE = 'test'

jit.opt.start('hotloop=1')

local counter = 0
local slot = 'slot'
-- XXX: The loop is limited to 3 iterations to compile a trace and
-- start to execute it. Use the `while true do` loop format to see
-- the side effects on the restoration from the snapshot.
while true do
  counter = counter + 1
  -- Use a non-constant slot to emit `FREF` with `IRFL_TAB_NOMM`.
  -- After re-emitting the variant part of the loop, NEWREF will
  -- contain a constant key (see below).
  slot = {[slot] = TEST_VALUE}
  -- Emit exit here to be sure that the table will be restored
  -- from the snapshot.
  if counter > 2 then break end
  -- We need a constant reference for NEWREF. Just use the old
  -- value.
  slot = 'slot'
end

test:is(slot.slot, TEST_VALUE, 'correct table content')
test:ok(debug.getmetatable(slot) == nil, 'no metatable on the restored table')

test:done(true)
