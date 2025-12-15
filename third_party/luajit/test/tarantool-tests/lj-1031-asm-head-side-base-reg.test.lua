local tap = require('tap')
-- Test file to demonstrate incorrect side trace head assembling
-- when use parent trace register holding base (`RID_BASE`).
-- See also, https://github.com/LuaJIT/LuaJIT/issues/1031.
--
-- XXX: For now, the test doesn't fail even on arm64 due to the
-- different from upstream register allocation. Nevertheless, this
-- issue should be gone with future backporting, so just leave the
-- test case as is.
local test = tap.test('lj-1031-asm-head-side-base-reg'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local ffi = require('ffi')
local int64_t = ffi.typeof('int64_t')

test:plan(1)

-- To reproduce the issue (reproduced only on arm64) we need a
-- little bit of a tricky structure for the traces:
-- +-> + -- start TRACE 1
-- |   |    ...
-- |   |---> + -- start TRACE 3 (side trace for the 1st)
-- |   |     | ...
-- |   |     v (link with TRACE 2)
-- |   | +-> + -- start TRACE 2 (some loop that wasn't recorded
-- |   | |   |                   before due to the loop limit)
-- |   | +---+
-- +<--+
-- Also, we need high register pressure to be sure that register
-- holding value of the base will be spilled.
-- So, during the assembly of "TRACE 3" (#6 in our test),
-- `RID_BASE` is spilled and restored via 32-bit fpr. This leads
-- to an incorrect value because it contains a 64-bit value.
-- See the original ticket for the details.

-- XXX: Reduce amount of IR.
local tonumber = tonumber
local ffi_new = ffi.new

-- Resulting slots for values calculated on trace.
-- luacheck: ignore
local r1
local r2
local r3
local r4
local r5
local r6
local r7
local r8
local r9
local r10
local r11
local r12
local r13
local r14
local r15
local r16
local r17
local r18
local r19

local ARR_SIZE = 100
local lim_arr = {}

-- XXX: Prevent irrelevant output in jit.dump().
jit.off()

-- `INNER_LIMIT1` - no cycle is taken.
-- `INNER_LIMIT2` - cycle is taken and compiled.
-- `INNER_TRACE_LIMIT` - empirical number of iterations to compile
-- all necessary traces from the outer cycle.
local INNER_TRACE_LIMIT = 20
local INNER_LIMIT1 = 1
local INNER_LIMIT2 = 4
for i = 1, INNER_TRACE_LIMIT do
  lim_arr[i] = INNER_LIMIT1
end
for i = INNER_TRACE_LIMIT + 1, ARR_SIZE + 1 do
  lim_arr[i] = INNER_LIMIT2
end

-- Enable compilation back.
jit.on()

-- XXX: `hotexit` is set to 2 to decrease the number of
-- meaningless side traces.
jit.opt.start('hotloop=1', 'hotexit=2')

-- XXX: Trace numbers are given with the respect of using
-- `jit.dump`.
-- Start TRACE 2 (1 is inside dump bc).
local k = 0
while k < ARR_SIZE do
  k = k + 1
  -- Forcify register pressure.
  local l1  = ffi_new(int64_t, k + 1)
  local l2  = ffi_new(int64_t, k + 2)
  local l3  = ffi_new(int64_t, k + 3)
  local l4  = ffi_new(int64_t, k + 4)
  local l5  = ffi_new(int64_t, k + 5)
  local l6  = ffi_new(int64_t, k + 6)
  local l7  = ffi_new(int64_t, k + 7)
  local l8  = ffi_new(int64_t, k + 8)
  local l9  = ffi_new(int64_t, k + 9)
  local l10 = ffi_new(int64_t, k + 10)
  local l11 = ffi_new(int64_t, k + 11)
  local l12 = ffi_new(int64_t, k + 12)
  local l13 = ffi_new(int64_t, k + 13)
  local l14 = ffi_new(int64_t, k + 14)
  local l15 = ffi_new(int64_t, k + 15)
  local l16 = ffi_new(int64_t, k + 16)
  local l17 = ffi_new(int64_t, k + 17)
  local l18 = ffi_new(int64_t, k + 18)
  local l19 = ffi_new(int64_t, k + 19)
  -- Side exit for TRACE 6 start 2/1.
  -- luacheck: ignore
  -- XXX: The number is meaningless, just needs to be big enough
  -- to be sure that all necessary traces are compiled.
  if k > 55 then else end
  r1  = tonumber(l1)
  r2  = tonumber(l2)
  r3  = tonumber(l3)
  r4  = tonumber(l4)
  r5  = tonumber(l5)
  r6  = tonumber(l6)
  r7  = tonumber(l7)
  r8  = tonumber(l8)
  r9  = tonumber(l9)
  r10 = tonumber(l10)
  r11 = tonumber(l11)
  r12 = tonumber(l12)
  r13 = tonumber(l13)
  r14 = tonumber(l14)
  r15 = tonumber(l15)
  r15 = tonumber(l15)
  r16 = tonumber(l16)
  r17 = tonumber(l17)
  r18 = tonumber(l18)
  r19 = tonumber(l19)
  local lim_inner = lim_arr[k]
  -- TRACE 3. Loop is not taken at the moment of recording TRACE 2
  -- (lim_inner == 1).
  for _ = 1, lim_inner do
    -- TRACE 6 stop -> 3.
  end
end

test:ok(true, 'correct side trace head assembling')

test:done(true)
