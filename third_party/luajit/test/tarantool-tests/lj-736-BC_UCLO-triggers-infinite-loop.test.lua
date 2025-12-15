local tap = require('tap')
-- Test file to demonstrate the infinite loop in LuaJIT during the
-- use-def analysis for upvalues.
-- See details in https://github.com/LuaJIT/LuaJIT/issues/736.
local test = tap.test('lj-736-BC_UCLO-triggers-infinite-loop'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})
test:plan(2)

-- Before the patch, the code flow like in the `testcase()` below
-- may cause the problem -- use-def analysis for the 0019 UCLO
-- creates an infinite loop in 0014 - 0019:
-- | 0008 FORI   base:    4 jump:  => 0013
-- | 0009 ISNEN  var:     7 num:     0 ; number 2
-- | 0010 JMP    rbase:   8 jump:  => 0012
-- | 0011 UCLO   rbase:   2 jump:  => 0014
-- | 0012 FORL   base:    4 jump:  => 0009
-- | 0013 UCLO   rbase:   2 jump:  => 0014
-- | 0014 KPRI   dst:     2 pri:     0 ; Start of `assert()` line.
-- | ...
-- | 0019 UCLO   rbase:   2 jump:  => 0014

jit.opt.start('hotloop=1')

local assert_msg = 'Infinite loop is not reproduced.'
local assert = assert

local function testcase()
  -- The code in the first scope `do`/`end` is a prerequisite.
  -- It contains the UCLO instruction for the `uv1`. The use-def
  -- analysis for it escapes this `do`/`end` scope.
  do
    local uv1 -- luacheck: no unused
    local _ = function() return uv1 end

    -- Records the trace for which use-def analysis is applied.
    for i = 1, 2 do
      -- This condition triggers snapshotting and use-def
      -- analysis. Before the patch this triggers the infinite
      -- loop in the `snap_usedef()`, so the `goto` is never
      -- taken.
      if i == 2 then
        goto x
      end
    end
  end

::x::
  do
    local uv2 -- luacheck: no unused

    -- Create a tight loop for the one more upvalue (`uv2`).
    -- Before the patch, use-def analysis gets stuck in this code
    -- flow.
    assert(nil, assert_msg)
    goto x
    -- This code is unreachable by design.
    local _ = function() return uv2 end -- luacheck: ignore
  end
end

local ok, err = pcall(testcase)

test:is(ok, false, 'assertion is triggered in a function with testcase')
test:ok(err:match(assert_msg), 'BC_UCLO does not trigger an infinite loop')

test:done(true)
