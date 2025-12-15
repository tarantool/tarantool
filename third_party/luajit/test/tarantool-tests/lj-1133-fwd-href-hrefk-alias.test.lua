local tap = require('tap')

-- Test file to demonstrate the LuaJIT's incorrect aliasing check
-- for HREFK and HREF IRs during the non-nil check.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1133.

local test = tap.test('lj-1133-fwd-href-hrefk-alias'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})
test:plan(1)

local rawset = rawset

-- The maximum value that can be stored in a 16-bit `op2`
-- field in HREFK IR.
local HASH_NODES = 65535

-- Amount of iterations to compile and execute the trace.
local LOOP_LIMIT = 4

-- Function to be called twice to emit the trace and take the side
-- exit.
local function trace_aliased_tables(t1, t2)
  -- The criteria is the number of new index creations.
  local count = 0
  local mt = {__newindex = function(t, k, v)
    count = count + 1
    rawset(t, k, v)
  end}
  setmetatable(t1, mt)
  setmetatable(t2, mt)

  for _ = 1, LOOP_LIMIT do
    -- XXX: Keys values have no special meaning here, just be sure
    -- that they are HREF/HREFK and not in the array table part.
    -- `t1` is empty, emitting HREFK.
    t1[10] = 1
    -- `t2` on recording has more than `HASH_NODES` table nodes,
    -- so this emits HREF.
    t2[10] = nil
    -- Resolve `__newindex` if t1 == t2.
    -- `lj_opt_fwd_wasnonnil()` missed the check that HREFK and
    -- HREF may alias before the patch, so the guarded HLOAD IR
    -- with the corresponding snapshot is skipped.
    -- The difference in the emitted IR before and after the patch
    -- is the following:
    -- |  0004 >  tab SLOAD  #1    T
    -- |              ...
    -- |  0009    p32 FLOAD  0004  tab.node
    -- |  0010 >  p32 HREFK  0009  +10  @0
    -- |  0011 >  num HLOAD  0010
    -- |  0012    num HSTORE 0010  +1
    -- |  ....        SNAP   #1
    -- |  0013 >  tab SLOAD  #2    T
    -- |  0014    int FLOAD  0013  tab.asize
    -- |  0015 >  int ULE    0014  +10
    -- |  0016    p32 HREF   0013  +10
    -- |  0017 >  p32 NE     0016  [0x415554e8]
    -- |  0018 >  num HLOAD  0016
    -- |  0019    nil HSTORE 0016  nil
    -- | -0020    num HSTORE 0010  +30
    -- |  ....        SNAP   #2
    -- | +0020 >  num HLOAD  0010
    -- | +0021    num HSTORE 0010  +30
    -- | +....        SNAP   #3
    --
    -- Hence, the taken exit is not resolving `__newindex` before
    -- the patch.
    t1[10] = 1
    -- The exit 2 of the trace is here.
    -- Resolve `__newindex` if t1 ~= t2.
    t2[10] = 1
  end
  -- `__newindex` is called twice on the first iteration and once
  -- on each other.
  return count == LOOP_LIMIT + 1
end

-- Create a big table to emit HREF IR (not HREFK) to trick
-- the alias checking.
local bigt = {}
for i = 1, HASH_NODES + 1 do
  bigt[-i] = true
end

jit.opt.start('hotloop=1')

trace_aliased_tables({}, bigt)

-- Now use tables that are aliased.
local smallt = {}
test:ok(trace_aliased_tables(smallt, smallt), 'aliasing check is correct')

test:done(true)
