local tap = require('tap')

-- Test file to demonstrate LuaJIT misbehaviour for NEWREF IR
-- taken NaN value.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/1069.

local test = tap.test('lj-1069-newref-nan-key'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local table_new = require('table.new')
local ffi = require('ffi')

local NaN = tonumber('nan')

test:plan(4)

test:test('NaN on trace in the non-constant IR', function(subtest)
  local NKEYS = 3

  -- XXX: NaN isn't stored, so the number of tests is:
  -- (NKEYS - 1) + test status + test error message + test keys
  -- amount.
  subtest:plan(NKEYS + 2)

  local tset_table = table_new(0, NKEYS)

  local function tset(t, k)
    -- Value doesn't matter.
    t[k] = true
  end

  -- Compile the function.
  jit.opt.start('hotloop=1')

  -- Use number keys to emit NEWREF.
  tset(tset_table, 0.1)
  tset(tset_table, 0.2)

  -- Insert NaN on the trace.
  local ok, err = pcall(tset, tset_table, NaN)

  subtest:ok(not ok, 'function returns an error')
  subtest:like(err, 'table index is NaN', 'correct error message')

  local nkeys = 0
  for k in pairs(tset_table) do
    nkeys = nkeys + 1
    subtest:ok(k == k, ('not NaN key by number %d'):format(nkeys))
  end
  subtest:is(nkeys, NKEYS - 1, 'correct amount of keys')
end)

test:test('NaN on trace in the non-constant IR CONV', function(subtest)
  -- XXX: simplify `jit.dump()` output.
  local tonumber = tonumber

  local NKEYS = 3

  -- XXX: NaN isn't stored, so the number of tests is:
  -- (NKEYS - 1) + test status + test error message + test keys
  -- amount.
  subtest:plan(NKEYS + 2)

  local tset_table = table_new(0, NKEYS)

  local function tset(t, k)
    -- XXX: Emit CONV to number type. Value doesn't matter.
    t[tonumber(k)] = true
  end

  -- Compile the function.
  jit.on()
  jit.opt.start('hotloop=1')

  -- Use number keys to emit NEWREF.
  tset(tset_table, ffi.new('float', 0.1))
  tset(tset_table, ffi.new('float', 0.2))

  -- Insert NaN on the trace.
  local ok, err = pcall(tset, tset_table, ffi.new('float', NaN))

  subtest:ok(not ok, 'function returns an error')
  subtest:like(err, 'table index is NaN', 'correct error message')

  local nkeys = 0
  for k in pairs(tset_table) do
    nkeys = nkeys + 1
    subtest:ok(k == k, ('not NaN key by number %d'):format(nkeys))
  end
  subtest:is(nkeys, NKEYS - 1, 'correct amount of keys')
end)

-- Test the constant IR NaN value on the trace.

test:test('constant NaN on the trace', function(subtest)
  -- Test the status and the error message.
  subtest:plan(2)
  local function protected()
    local counter = 0
    -- Use a number key to emit NEWREF.
    local key = 0.1

    jit.opt.start('hotloop=1')

    while counter < 2 do
      counter = counter + 1
      -- luacheck: ignore
      local tab = {_ = 'unused'}
      tab[key] = 'NaN'
      -- XXX: Use the constant NaN value on the trace.
      key = 0/0
    end
  end

  local ok, err = pcall(protected)
  subtest:ok(not ok, 'function returns an error')
  subtest:like(err, 'table index is NaN', 'NaN index error message')
end)

test:test('constant NaN on the trace and rehash of the table', function(subtest)
  -- A little bit different test case: after rehashing the table,
  -- the node is lost, and a hash part of the table isn't freed.
  -- This leads to the assertion failure, which checks memory
  -- leaks on shutdown.
  -- XXX: The test checks didn't fail even before the patch. Check
  -- the same values as in the previous test for consistency.
  subtest:plan(2)
  local function protected()
    local counter = 0
    -- Use a number key to emit NEWREF.
    local key = 0.1

    jit.opt.start('hotloop=1')

    while counter < 2 do
      counter = counter + 1
      -- luacheck: ignore
      local tab = {_ = 'unused'}
      tab[key] = 'NaN'
      -- Rehash the table.
      tab[counter] = 'unused'
      -- XXX: Use the constant NaN value on the trace.
      key = 0/0
    end
  end

  local ok, err = pcall(protected)
  subtest:ok(not ok, 'function returns an error')
  subtest:like(err, 'table index is NaN', 'NaN index error message')
end)

test:done(true)
