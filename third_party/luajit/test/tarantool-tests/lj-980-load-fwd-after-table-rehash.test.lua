local tap = require('tap')

-- Test file to demonstrate LuaJIT misbehaviour during load
-- forwarding optimization for HLOAD after table rehashing.
-- See also https://github.com/LuaJIT/LuaJIT/issues/980.

local test = tap.test('lj-980-load-fwd-after-table-rehash'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(6)

jit.opt.start('hotloop=1')

local result
-- The test for TNEW load forwarding. It doesn't trigger an assert
-- since the commit "Fix TNEW load forwarding with instable
-- types.". But is still added it to avoid regressions in the
-- future.
for i = 6, 9 do
  -- Need big enough table to see rehashing.
  -- Also, to simplify logic with AREF, HREF don't use default
  -- 1, 4 (start, stop) values here.
  local t = {i, i, i, i, i, i, i}
  -- Insert via ASTORE.
  t[i] = i
  t[1] = nil
  t[2] = nil
  t[3] = nil
  t[4] = nil
  t[5] = nil
  -- Rehash table. Array part is empty.
  t['1000'] = 1000
  -- Load via HLOAD.
  result = t[i]
end

test:is(result, 9, 'TNEW load forwarding')

-- TNEW load forwarding, aliased table. It doesn't trigger an
-- assert since the commit "Fix TNEW load forwarding with instable
-- types.". But is still added it to avoid regressions in the
-- future.
local alias_store = {{}, {}, {}, {}, {}}
for i = 6, 9 do
  local t = {i, i, i, i, i, i, i}
  alias_store[#alias_store + 1] = t
  local alias = alias_store[i]
  -- Insert via ASTORE.
  alias[i] = i
  alias[1] = nil
  alias[2] = nil
  alias[3] = nil
  alias[4] = nil
  alias[5] = nil
  -- Rehash table. Array part is empty.
  alias['1000'] = 1000
  -- Load via HLOAD.
  result = t[i]
end

test:is(result, 9, 'TNEW load forwarding, aliased table')

local expected = 'result'

-- TDUP different types.
for i = 6, 9 do
  local t = {1, 2, 3, 4, 5, 6, 7, 8}
  t[i] = expected
  t[i + 1] = expected
  t[1] = nil
  t[2] = nil
  t[3] = nil
  t[4] = nil
  t[5] = nil
  t[6] = nil
  -- Rehash table. Array part is empty.
  t['1000'] = 1000
  -- Result on the recording (i == 8) iteration is 'result'.
  -- Nevertheless, on the last (i == 9) iteration it is 8.
  -- Just check that there is no assert failure here.
  -- Load via HLOAD.
  result = t[8]
end

-- Checked for assertion guard, on the last iteration we get
-- the value on initializatoin.
test:is(result, 8, 'TDUP load forwarding different types')

-- TDUP different types, aliased table.
alias_store = {{}, {}, {}, {}, {}}
for i = 6, 9 do
  local t = {1, 2, 3, 4, 5, 6, 7, 8}
  -- Store table, to be aliased later.
  alias_store[#alias_store + 1] = t
  local alias = alias_store[i]
  alias[i] = expected
  alias[i + 1] = expected
  alias[1] = nil
  alias[2] = nil
  alias[3] = nil
  alias[4] = nil
  alias[5] = nil
  alias[6] = nil
  -- Rehash table. Array part is empty.
  alias['1000'] = 1000
  -- Result on the recording (i == 8) iteration is 'result'.
  -- Nevertheless, on the last (i == 9) iteration it is 8.
  -- Just check that there is no assert failure here.
  -- Load via HLOAD.
  result = t[8]
end

-- Checked for assertion guard, on the last iteration we get
-- the value on initializatoin.
test:is(result, 8, 'TDUP load forwarding different types, aliased table')

-- TDUP same type, different values.
for i = 6, 9 do
  local t = {1, 2, 3, 4, 5, 6, '7', '8'}
  t[i] = expected
  t[i + 1] = expected
  t[1] = nil
  t[2] = nil
  t[3] = nil
  t[4] = nil
  t[5] = nil
  t[6] = nil
  -- Rehash table. Array part is empty.
  t['1000'] = 1000
  -- Result on the recording (i == 8) iteration is 'result'.
  -- Nevertheless, on the last (i == 9) iteration it is '8'.
  -- Just check that there is no assert failure here.
  -- Load via HLOAD.
  result = t[8]
end

-- Checked for assertion guard, on the last iteration we get
-- the value on initializatoin.
test:is(result, '8', 'TDUP load forwarding same type, different values')

alias_store = {{}, {}, {}, {}, {}}
for i = 6, 9 do
  local t = {1, 2, 3, 4, 5, 6, '7', '8'}
  -- Store table, to be aliased later.
  alias_store[#alias_store + 1] = t
  local alias = alias_store[i]
  alias[i] = expected
  alias[i + 1] = expected
  alias[1] = nil
  alias[2] = nil
  alias[3] = nil
  alias[4] = nil
  alias[5] = nil
  alias[6] = nil
  -- Rehash table. Array part is empty.
  alias['1000'] = 1000
  -- Result on the recording (i == 8) iteration is 'result'.
  -- Nevertheless, on the last (i == 9) iteration it is '8'.
  -- Just check that there is no assert failure here.
  -- Load via HLOAD.
  result = t[8]
end

-- Checked for assertion guard, on the last iteration we get
-- the value on initializatoin.
test:is(result, '8',
        'TDUP load forwarding same type, different values, aliased table')

test:done(true)
