local tap = require('tap')

-- Test file to demonstrate incorrect HREFK optimization
-- in LuaJIT.

local ffi = require('ffi')
local test = tap.test('lj-840-gc64-fix-hrefk-optimization'):skipcond({
  ['Test requires GC64 mode enabled'] = not ffi.abi('gc64'),
  ['Test requires JIT enabled'] = not jit.status(),
})
test:plan(1)

local table_new = require('table.new')

local SIZEOF_GCTAB = 64
-- See `chunk2mem` in lj_alloc.c for details.
local ALLOC_CHUNK_HDR = 16
local GCTAB_FOOTPRINT = SIZEOF_GCTAB + ALLOC_CHUNK_HDR
-- Size of single hash node in bytes.
local NODE_SIZE = 24
-- The maximum value that can be stored in a 16-bit `op2`
-- field in HREFK IR.
local HASH_NODES = 65535
-- The vector of hash nodes should have a raw size of
-- `HASH_NODES * NODE_SIZE`, which is allocated in
-- `lj_alloc_malloc` directly with `mmap`. However,
-- the LuaJIT allocator adds a bunch of small paddings
-- and aligns the required size to LJ_PAGESIZE, which is
-- 4096, so the actual allocated size includes alignment.
local LJ_PAGESIZE = 4096
-- The vector for hash nodes in the table is allocated based on
-- `hbits`, so it's actually got a size of 65536 nodes.
local NODE_VECTOR_SIZE = (HASH_NODES + 1) * NODE_SIZE + LJ_PAGESIZE
local SINGLE_ITERATION_ALLOC = NODE_VECTOR_SIZE + GCTAB_FOOTPRINT
-- We need to overflow the 32-bit distance to the global nilnode,
-- so we divide 2^32 by the SINGLE_ITERATION_ALLOC and ceil
-- the result.
local N_ITERATIONS = math.ceil((2 ^ 32) / SINGLE_ITERATION_ALLOC)
-- Prevent anchor table from interfering with target
-- table allocations.
local anchor = table.new(N_ITERATIONS, 0)

-- Construct table.
for _ = 1, N_ITERATIONS do
  table.insert(anchor, table_new(0, HASH_NODES))
end

jit.opt.start('hotloop=1')
local function get_n(tab)
  local x
  for _ = 1, 4 do
    x = tab.n
  end
  return x
end

-- Record the trace for the constructed table.
get_n(anchor[#anchor])

-- Check the result for the table that has the required key.
local result = get_n({n=1})
test:is(result, 1, 'correct value retrieved')
test:done(true)
