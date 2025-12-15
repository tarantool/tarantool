local tap = require('tap')

-- Test file to demonstrate LuaJIT incorrect optimizations across
-- the `table.clear()` call.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/792.

local test = tap.test('lj-792-hrefk-table-clear'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})
local table_clear = require('table.clear')

test:plan(7)

local NITERATIONS = 4
local MAGIC = 42

local function test_aref_fwd_tnew(tab_number)
  local field_value_after_clear
  for i = 1, NITERATIONS do
    -- Create a table on trace to make the optimization work.
    -- Initialize the first field to work with the array part.
    local tab = {i}
    -- Use an additional table to alias the created table with the
    -- `1` key.
    local tab_array = {tab, {0}}
    -- AREF to be forwarded.
    tab[1] = MAGIC
    table_clear(tab_array[tab_number])
    -- It should be `nil`, since table is cleared.
    field_value_after_clear = tab[1]
  end
  return field_value_after_clear
end

local function test_aref_fwd_tdup(tab_number)
  local field_value_after_clear
  for _ = 1, NITERATIONS do
    -- Create a table on trace to make the optimization work.
    local tab = {nil}
    -- Use an additional table to alias the created table with the
    -- `1` key.
    local tab_array = {tab, {0}}
    -- AREF to be forwarded.
    tab[1] = MAGIC
    table_clear(tab_array[tab_number])
    -- It should be `nil`, since table is cleared.
    field_value_after_clear = tab[1]
  end
  return field_value_after_clear
end

local function test_href_fwd_tnew(tab_number)
  local field_value_after_clear
  for _ = 1, NITERATIONS do
    -- Create a table on trace to make the optimization work.
    local tab = {}
    -- Use an additional table to alias the created table with the
    -- `8` key.
    local tab_array = {tab, {0}}
    -- HREF to be forwarded. Use 8 to be in the hash part.
    tab[8] = MAGIC
    table_clear(tab_array[tab_number])
    -- It should be `nil`, since table is cleared.
    field_value_after_clear = tab[8]
  end
  return field_value_after_clear
end

local function test_href_fwd_tdup(tab_number)
  local field_value_after_clear
  for _ = 1, NITERATIONS do
    -- Create a table on trace to make the optimization work.
    local tab = {nil}
    -- Use an additional table to alias the created table with the
    -- `8` key.
    local tab_array = {tab, {0}}
    -- HREF to be forwarded. Use 8 to be in the hash part.
    tab[8] = MAGIC
    table_clear(tab_array[tab_number])
    -- It should be `nil`, since table is cleared.
    field_value_after_clear = tab[8]
  end
  return field_value_after_clear
end

local function test_not_forwarded_hrefk_val_from_newref(tab_number)
  local field_value_after_clear
  for _ = 1, NITERATIONS do
    -- Create a table on trace to make the optimization work.
    local tab = {}
    -- NEWREF to be forwarded.
    tab.hrefk = MAGIC
    -- Use an additional table to alias the created table with the
    -- `hrefk` key.
    local tab_array = {tab, {hrefk = 0}}
    table_clear(tab_array[tab_number])
    -- It should be `nil`, since it is cleared.
    field_value_after_clear = tab.hrefk
  end
  return field_value_after_clear
end

local function test_not_dropped_guard_on_hrefk(tab_number)
  local tab, field_value_after_clear
  for _ = 1, NITERATIONS do
    -- Create a table on trace to make the optimization work.
    tab = {hrefk = MAGIC}
    -- Use an additional table to alias the created table with the
    -- `hrefk` key.
    local tab_array = {tab, {hrefk = 0}}
    table_clear(tab_array[tab_number])
    -- It should be `nil`, since it is cleared.
    -- If the guard is dropped for HREFK, the value from the TDUP
    -- table is taken instead, without the type check. This leads
    -- to incorrectly returned (swapped) values.
    field_value_after_clear = tab.hrefk
    tab.hrefk = MAGIC
  end
  return field_value_after_clear, tab.hrefk
end

jit.opt.start('hotloop=1')

-- First, compile the trace that clears the not-interesting table.
test_aref_fwd_tnew(2)
-- Now run the trace and clear the table, from which we take AREF.
test:is(test_aref_fwd_tnew(1), nil, 'AREF forward from TNEW')

-- XXX: Reset hotcounters to avoid collisions.
jit.opt.start('hotloop=1')

-- First, compile the trace that clears the not-interesting table.
test_aref_fwd_tdup(2)
-- Now run the trace and clear the table, from which we take AREF.
test:is(test_aref_fwd_tdup(1), nil, 'AREF forward from TDUP')

-- XXX: Reset hotcounters to avoid collisions.
jit.opt.start('hotloop=1')

-- First, compile the trace that clears the not-interesting table.
test_href_fwd_tnew(2)
-- Now run the trace and clear the table, from which we take HREF.
test:is(test_href_fwd_tnew(1), nil, 'HREF forward from TNEW')

-- XXX: Reset hotcounters to avoid collisions.
jit.opt.start('hotloop=1')

-- First, compile the trace that clears the not-interesting table.
test_href_fwd_tdup(2)
-- Now run the trace and clear the table, from which we take HREF.
test:is(test_href_fwd_tdup(1), nil, 'HREF forward from TDUP')

-- XXX: Reset hotcounters to avoid collisions.
jit.opt.start('hotloop=1')

-- First, compile the trace that clears the not-interesting table.
test_not_forwarded_hrefk_val_from_newref(2)
-- Now run the trace and clear the table, from which we take
-- HREFK.
local value_from_cleared_tab = test_not_forwarded_hrefk_val_from_newref(1)

test:is(value_from_cleared_tab, nil,
        'not forward the field value across table.clear')

-- XXX: Reset hotcounters to avoid collisions.
jit.opt.start('hotloop=1')

-- First, compile the trace that clears the not-interesting table.
test_not_dropped_guard_on_hrefk(2)
-- Now run the trace and clear the table, from which we take
-- HREFK.
local field_value_after_clear, tab_hrefk = test_not_dropped_guard_on_hrefk(1)

test:is(field_value_after_clear, nil, 'correct field value after table.clear')
test:is(tab_hrefk, MAGIC, 'correct value set in the table that was cleared')

test:done(true)
