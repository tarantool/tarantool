local tap = require('tap')

-- Test file to demonstrate LuaJIT's incorrect hoisting out of the
-- loop for Array Bound Check.
-- See also https://github.com/LuaJIT/LuaJIT/issues/1194.

local test = tap.test('lj-1194-abc-hoisting'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local table_new = require('table.new')

-- Before the patch, the `for` cycle in the `test_func()` below
-- produces the following trace:
--
-- | 0006    int FLOAD  0005  tab.asize
-- | 0007 >  p32 ABC    0006  0001
-- | 0008    p32 FLOAD  0005  tab.array
-- | 0009    p32 AREF   0008  0003
-- | 0010    tab FLOAD  0005  tab.meta
-- | 0011 >  tab EQ     0010  NULL
-- | 0012    nil ASTORE 0009  nil
-- | 0013 >+ tab TNEW   #0    #0
-- | 0014  + int ADD    0003  +1
-- | 0015 >  int LE     0014  0001
-- | 0016 ------ LOOP ------------
-- | 0017 >  int NE     0014  +0
-- | 0018    p32 FLOAD  0013  tab.array
-- | 0019    p32 AREF   0018  0014
-- | 0020    nil ASTORE 0019  nil
--
-- As you can see, the `0007 ABC` instruction is dropped from the
-- variant part of the loop.

-- Disable fusing to simplify the reproducer it now will be crash
-- on loading of an address from the `t->array`.
jit.opt.start('hotloop=1', '-fuse')

local function test_func()
  -- The first iteration for hotcount warm-up. The second and
  -- third are needed to record invariant and variant parts of the
  -- loop. The trace is run via an additional call to this
  -- function.
  local limit = 3
  -- Create a table with a fixed array size (`limit` + 1), so all
  -- access to it fits into the array part.
  local s = table_new(limit, 0)
  for i = 1, limit do
    -- Don't change the table on hotcount warm-up.
    if i ~= 1 then
      -- `0020 ASTORE` causes the SegFault on the trace on the
      -- last (3rd) iteration, since the table (set on the first
      -- iteration) has `asize == 0`, and t->array == NULL`.
      -- luacheck: no unused
      s[i] = nil
      s = {}
    end
  end
end

-- Compile the `for` trace inside `test_func()`.
-- The invariant part of this trace contains the ABC check, while
-- the invariant does not. So, first compile the trace, then use
-- the second call to run it from the beginning with all guards
-- passing in the invariant part.
test_func()

-- Avoid compilation of any other traces.
jit.off()

-- Run that trace.
test_func()

test:ok(true, 'no crash on the trace due to incorrect ABC hoisting')

test:done(true)
