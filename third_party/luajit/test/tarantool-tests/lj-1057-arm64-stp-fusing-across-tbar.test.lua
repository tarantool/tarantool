local tap = require('tap')

-- This test demonstrates LuaJIT's incorrect fusing of store
-- instructions separated by the conditional branch on arm64.
-- See also https://github.com/LuaJIT/LuaJIT/issues/1057.
local test = tap.test('lj-1057-arm64-stp-fusing-across-tbar'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

-- XXX: Simplify the `jit.dump()` output.
local setmetatable = setmetatable

-- The function below generates the following IR:
-- | 0011          p64 FREF   0003  tab.meta
-- | ...
-- | 0018 x0    >  tab TNEW   #0    #0
-- | 0019          tab TBAR   0003
-- | 0020          tab FSTORE 0011  0018
-- The expected mcode to be emitted for the last two IRs is the
-- following:
-- | 55626cffb0  ldrb  w30, [x19, #8] ; tab->marked
-- | 55626cffb4  tst   w30, #0x4      ; Is black?
-- | 55626cffb8  beq   0x626cffd0     ; Skip marking.
-- | 55626cffbc  ldr   x27, [x20, #128]
-- | 55626cffc0  and   w30, w30, #0xfffffffb
-- | 55626cffc4  str   x19, [x20, #128]
-- | 55626cffcc  strb  w30, [x19, #8]  ; tab->marked
-- | 55626cffc8  str   x27, [x19, #24] ; tab->gclist
-- | 55626cffd0  str   x0,  [x19, #32] ; tab->metatable
--
-- But the last 2 instructions are fused into the following `stp`:
-- | 55581dffd0  stp   x27, x0, [x19, #48]
-- Hence, the GC propagation frontier back is done partially,
-- since `str x27, [x19, #24]` is not skipped despite TBAR
-- semantics. This leads to the incorrect value in the `gclist`
-- and the segmentation fault during its traversal on GC step.
local function trace(target_t)
  -- Precreate a table for the FLOAD to avoid TNEW in between.
  local stack_t = {}
  -- Generate FSTORE TBAR pair. The FSTORE will be dropped due to
  -- the FSTORE below by DSE.
  setmetatable(target_t, {})
  -- Generate FSTORE. TBAR will be dropped by CSE.
  setmetatable(target_t, stack_t)
end

jit.opt.start('hotloop=1')

-- XXX: Need to trigger the GC on trace to introspect that the
-- GC chain is broken. Use empirical 10000 iterations.
local tab = {}
for _ = 1, 1e4 do
  trace(tab)
end

test:ok(true, 'no assertion failure in the simple loop')

-- The similar test, but be sure that we finish the whole GC
-- cycle, plus using upvalue instead of stack slot for the target
-- table.

local target_t = {}
local function trace2()
  local stack_t = {}
  setmetatable(target_t, {})
  setmetatable(target_t, stack_t)
end

collectgarbage('collect')
collectgarbage('setstepmul', 1)
while not collectgarbage('step') do
  trace2()
end

test:ok(true, 'no assertion failure in the whole GC cycle')

test:done(true)
