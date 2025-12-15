local tap = require('tap')

-- Test file to demonstrate LuaJIT bug with constant
-- rematerialization on arm64.
-- See also https://github.com/LuaJIT/LuaJIT/pull/438.
local test = tap.test('lj-438-arm64-constant-rematerialization'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})
test:plan(1)

-- This test file demonstrates the following problem:
-- The assembly of an IR instruction allocates a constant into a
-- free register. Then it spills another register (due to high
-- register pressure), which is rematerialized using the same
-- constant (which it assumes is now in the allocated register).
-- In case when the first register also happens to be the
-- destination register, the constant value is modified before the
-- rematerialization.
--
-- For the code below we get the following register allocation
-- order (read from top to bottom (DBG RA reversed)):
-- | current IR | operation | IR ref | register
-- |  0048         alloc       0038     x0
-- |  0048         remat       K038     x0
-- |  0048         alloc       K023     x4
--
-- Which leads to the following assembly:
-- | ...
-- | add   x4, x4, x0    # x4 modified before x0 rematerialization
-- | ldrb  w4, [x4, #24]
-- | add   x0, x4, #24   # constant x0 rematerialization
-- | ...
-- As a result, the value register x0 holding is incorrect.

local empty = {}

jit.off()
jit.flush()

-- XXX: The example below is very fragile. Even the names of
-- the variables matter.
local function scan(vs)
  -- The code below is needed to generate high register pressure
  -- and specific register allocations.
  for _, v in ipairs(vs) do
    -- XXX: Just more usage of registers. Nothing significant.
    local sep = v:find('@')
    -- Recording of yielding `string.byte()` result encodes XLOAD
    -- IR. Its assembly modifies x4 register, that is chosen as
    -- a destination register.
    -- IR_NE, that using `asm_href()` uses the modified x4
    -- register as a source for constant x0 rematerialization.
    -- As far as it is modified before, the result value is
    -- incorrect.
    -- luacheck: ignore
    if v:sub(sep + 2, -2):byte() == 0x3f then -- 0x3f == '?'
    end

    -- XXX: Just more usage of registers. Nothing significant.
    local _ = empty[v]

    -- Here the `str` strdata value (rematerialized x0 register)
    -- given to the `lj_str_find()` is invalid on the trace,
    -- that as a result leading to the core dump.
    v:find(':')
  end
end

jit.on()
-- XXX: loopunroll is needed to avoid excess side trace generation
jit.opt.start('hotloop=1', 'loopunroll=1')

-- This wrapper function is needed to avoid excess errors 'leaving
-- loop in the root trace'.
local function wrap()
  -- XXX: There are four failing attempts to compile trace for
  -- this code:
  -- * The first trace trying to record starts with the ITERL BC
  --   in `scan()` function. The compilation failed, because
  --   recording starts at the second iteration, when the loop is
  --   left.
  -- * The second trace starts with UGET (scan) in the cycle
  --   below. Entering calling the `scan` function compilation
  --   failed, when sees the inner ITERL loop.
  -- * The third trace starts with GGET (ipairs) in the `scan()`
  --   function trying to record the hot function. The compilation
  --   is failed due to facing the inner ITERL loop.
  -- * At 19th iteration the ITERL trying to be recorded again
  --   after this instruction become hot again.
  --
  -- And, finally, at 39th iteration the `for` loop below is
  -- recorded after becoming hot again. Now the compiler inlining
  -- the inner loop and recording doesn't fail.
  -- The 40th iteration is needed to be sure the compiled mcode is
  -- correct.
  for _ = 1, 40 do
    scan({'ab@xyz'})
  end
end

wrap()

test:ok(true, 'the resulting trace is correct')

test:done(true)
