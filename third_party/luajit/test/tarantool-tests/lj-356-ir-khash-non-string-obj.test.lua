local tap = require('tap')
-- Test file to demonstrate the incorrect GC64 JIT behaviour
-- of an `IR_HREF` for the on-trace-constant key lookup.
-- See also https://github.com/LuaJIT/LuaJIT/pull/356.
local test = tap.test('lj-356-ir-khash-non-string-obj'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local N_ITERATIONS = 4

-- Amount of iteration for trace compilation and execution and
-- additional check, that there is no new trace compiled.
test:plan(N_ITERATIONS + 1)

local traceinfo = require('jit.util').traceinfo
local table_new = require('table.new')

-- To reproduce the issue we need to compile a trace with
-- `IR_HREF`, with a lookup of constant hash key GC value. To
-- prevent an `IR_HREFK` to be emitted instead, we need a table
-- with a huge hash part. Delta of address between the start of
-- the hash part of the table and the current node to lookup must
-- be more than `(1024 * 64 - 1) * sizeof(Node)`.
-- See <src/lj_record.c>, for details.
-- XXX: This constant is well suited to prevent test to be flaky,
-- because the aforementioned delta is always large enough.
local N_HASH_FIELDS = 1024 * 1024 * 8
local MAGIC = 42

local filled_tab = table_new(0, N_HASH_FIELDS + 1)

-- The function returns constant cdata pinned to `GCproto` to be
-- used as a key for table lookup.
local function get_const_cdata()
  return 0LL
end

-- XXX: don't set `hotexit` to prevent compilation of trace after
-- exiting the main test cycle.
jit.opt.start('hotloop=1')

-- Prevent `get_const_cdata()` from becoming hot and being
-- compiled before the main test cycle.
jit.off()

filled_tab[get_const_cdata()] = MAGIC

-- Speed up table filling-up.
jit.on()

-- Filling-up the table with GC values to minimize the amount of
-- hash collisions and increase delta between the start of the
-- hash part of the table and currently stored node.
for _ = 1, N_HASH_FIELDS do
  filled_tab[1LL] = 1
end

-- Prevent JIT misbehaviour before the main test chunk.
jit.off()

-- Allocate a table with exact array part to be sure that there
-- is no side exit from the trace, due to table reallocation.
local result_tab = table_new(N_ITERATIONS, 0)

jit.flush()

assert(not traceinfo(1), 'no traces compiled after flush')

jit.on()

for _ = 1, N_ITERATIONS do
  -- If the hash for table lookup is miscalculated, then we get
  -- `nil` (most possibly) value from the table and the side exit
  -- will be taken and we continue execution from the call to
  -- `get_const_cdata()`, this function is already hot after the
  -- first cycle iteration, and the new trace is recorded.
  table.insert(result_tab, filled_tab[get_const_cdata()])
end

jit.off()

test:ok(not traceinfo(2), 'the second trace should not be compiled')

-- No more need to prevent trace compilation.
jit.on()

for i = 1, N_ITERATIONS do
  -- Check that all lookups are correct and there is no
  -- value from other cdata stored in the table.
  test:ok(result_tab[i] == MAGIC, 'correct hash lookup from the table')
end

test:done(true)
