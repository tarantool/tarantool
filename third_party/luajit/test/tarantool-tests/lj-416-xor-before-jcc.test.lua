local tap = require('tap')
local test = tap.test('lj-416-xor-before-jcc'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- To reproduce this issue, we need:
-- 0) IR for either "internal" (e.g. type check, hmask check) or
--    "external" (e.g. branch or loop condition) guard begins to
--    be emitted to mcode.
-- 1) JCC to side exit is emitted to the trace mcode at the
--    beginning.
-- 2) Condition (i.e. comparison) is going to be emitted.
-- 3) Fuse optimization takes its place, that ought to allocate a
--    register for the load base.
-- 4) There are no free registers at this point.
-- 5) The one storing the constant zero is chosen to be sacrificed
--    and reallocated (consider the allocation cost in ra_alloc
--    for constant materialization).
-- 6) Before (or in the sense of trace execution, after) register
--    is being used in the aforementioned comparison, register
--    (r14 in our case) is reset by XOR emitted right after
--    (before) jump instruction.
-- 7) The comparison with fused load within is emitted.
--
-- This leads to assembly code like the following:
--   ucomisd xmm7, [r14]
--   xor r14d, r14d
--   jnb 0x555d3d250014        ->1
--
-- That xor is a big problem, as it modifies flags between the
-- ucomisd and the jnb, thereby causing the jnb to do the wrong
-- thing.

local ffi = require('ffi')
ffi.cdef[[
  int test_xor_func(int a, int b, int c, int d, int e, int f, void * g, int h);
]]
local testxor = ffi.load('libtestxor')

-- XXX: Unfortunately, these tricks are needed to create register
-- pressure and specific registers allocations.
local handler = setmetatable({}, {
  __newindex = function ()
    -- 0 and nil are suggested as different constant-zero values
    -- for the call and occupied different registers.
    testxor.test_xor_func(0, 0, 0, 0, 0, 0, nil, 0)
  end
})

local MAGIC = 42
local mconf = {
  { use = false, value = MAGIC + 1 },
  { use = true,  value = MAGIC + 1 },
}

local function testf()
  -- All code below is needed for generating register pressure.
  local value
  -- The first trace to compile is this for loop, with `rule.use`
  -- values is `true`.
  for _, rule in ipairs(mconf) do
    -- The side trace starts here, when `rule.use` value is
    -- `false`, returns to the `for` loop, where the function was
    -- called, starts another iteration, calls `testf()` again and
    -- ends at JITERL bytecode for the loop in this function.
    if rule.use then
      value = rule.value
      break
    end
  end

  -- luacheck: push no max_comment_line_length
  -- The code below is recorded with the following IRs:
  -- ....              SNAP   #1   [ lj-416-xor-before-jcc.test.lua:44|---- ]
  -- 0012       >  num UGT    0009  +42
  --
  -- That leads to the following assembly:
  -- ucomisd xmm7, [r14]
  -- xor r14d, r14d
  -- jnb 0x555d3d250014        ->1
  --
  -- As a result, this branch is taken due to the emitted `xor`
  -- instruction until the issue is not resolved.
  -- luacheck: pop
  if value <= MAGIC then
    return true
  end

  -- Nothing to do, just call testxor with many arguments.
  handler.nothing = 'to do'
end

-- We need to create long side trace to generate register
-- pressure (see the comment in `testf()`).
jit.opt.start('hotloop=1', 'hotexit=1')
for _ = 1, 3 do
  -- Don't use any `test` functions here to freeze the trace.
  assert(not testf())
end
test:ok(true, 'impossible branch is not taken')

test:done(true)
