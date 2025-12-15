local tap = require('tap')
local test = tap.test('lj-906-fix-err-mem'):skipcond({
  ['Test requires GC64 mode disabled'] = require('ffi').abi('gc64'),
})

test:plan(1)

local ffi = require('ffi')
local table_new = require('table.new')

local KB = 1024
local MB = 1024 * KB
local sizes = {8 * MB, 8 * KB, 8}

-- The maximum available table size, taking into account created
-- constants for one function and nested level.
local TNEW_SIZE = 511

local gc_anchor = {}

-- This function works until raises the error.
local function eat_chunks(size)
  -- Need raise the OOM error inside TDUP, not TNEW, so reserve
  -- memory for it.
  -- luacheck: no unused
  local tnew_anchor = table_new(TNEW_SIZE, 0)
  while true do
    table.insert(gc_anchor, ffi.new('char [?]', size))
  end
end

-- Function to format inner tab leading to TDUP emitting.
local function make_deep_table(inner_depth)
  local inner_tab = ''
  -- Repeate table template for TDUP.
  for _ = 1, inner_depth do
    inner_tab = inner_tab .. '{a ='
  end
  inner_tab = inner_tab .. '{}'
  for _ = 1, inner_depth do
    inner_tab = inner_tab .. '},'
  end
  return inner_tab
end

-- The `lj_err_mem()` doesn't fix `L->top`, when called from
-- helper function (like `lj_tab_dup()`) before the patch.
-- This function creates a chunk with many BC_TDUP inside.
local function make_TDUP_chunk()
  local big_tab = 'local _ = {\n'
  -- The maximum available table size, taking into account created
  -- constants for one function and nested level.
  local inner_tab = make_deep_table(128)
  for _ = 1, TNEW_SIZE do
    big_tab = big_tab .. inner_tab .. '\n'
  end
  big_tab = big_tab .. '}'
  return big_tab
end

local TDUP, err = loadstring(make_TDUP_chunk())
assert(TDUP, err)

-- Function  to create the additional frame to be rewritten later
-- in case of `lj_err_mem()` misbehaviour.
local function frame_before_TDUP()
  TDUP()
end

local function janitor()
  collectgarbage('collect')
  collectgarbage('stop')
end

janitor()

-- Avoid OOM on traces.
jit.off()

-- Stack slots are needed for coredump in case of misbehaviour.
-- luacheck: no unused
local unused1, unused2
-- Try to reserve all available memory with the give size tempo.
-- Begin with big chunks and finish with the small ones until all
-- memory is not allocated for cdata chunks.
for _, size in pairs(sizes) do
  pcall(eat_chunks, size)
  janitor()
end

-- Try to allocate memory for the table produced by TDUP.
-- XXX: It has to fail, but considering allocator specifics it
-- might succeed if `eat_chunks()` hits a memory fragmentation
-- issue, hence do not check the status of the `pcall()` below.
pcall(frame_before_TDUP)

-- Release memory for `tap` functions.
gc_anchor = nil
collectgarbage()

test:ok(true, 'correctly throw memory error')

test:done(true)
