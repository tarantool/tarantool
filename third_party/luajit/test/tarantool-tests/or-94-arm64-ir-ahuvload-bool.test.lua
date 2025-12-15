local tap = require('tap')
local test = tap.test('or-94-arm64-ir-ahuvload-bool'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})
-- Test file to demonstrate the incorrect JIT assembling
-- for IR_{AHUV}LOAD specialized to boolean.
-- See also: https://github.com/openresty/luajit2/pull/94.

-- If there is high register pressure, and there are almost
-- all registers in use during the aforementioned assembling,
-- the same register is chosen as the one holding the given
-- stack slot and the one holding the constant value for the type
-- comparison. As the result we get the following assertion
-- guard check in assembly:
-- | cmp   x0, x0, lsr #32
-- | bne   ->0
-- Which is always false.
local ffi = require('ffi')
-- Need for code generation.
_G.ffi = ffi

local traceinfo = require('jit.util').traceinfo

-- Each payload will be recording with the corresponding IR.
local TESTS = {
  {irname = 'ALOAD', payload = 'aload()'},
  {irname = 'HLOAD', payload = 'hload()'},
  {irname = 'ULOAD', payload = 'uload()'},
  {irname = 'VLOAD', payload = '...'},
}
local N_TESTS = #TESTS

test:plan(N_TESTS)

-- Functions to be inlined on trace to generate different
-- types of IRs (ALOAD, HLOAD, ULOAD).
-- Declare as global for code generation.
local arr = {true}
function _G.aload()
  return arr[1]
end

local h = {data = true}
function _G.hload()
  local boolvalue = h.data
  return boolvalue
end

do
  local upvalue = true
  function _G.uload()
    return upvalue
  end
  -- Make upvalue mutable. Not really need to return this
  -- function.
  local function _()
    upvalue = not upvalue
  end
end

-- This function generates code like the following:
-- | local test_f(...)
-- |   local r
-- |   local rup1
-- |   --[[...]]
-- |   for _ = 1, 4 do
-- |     r1 = ffi.cast("int", 1)
-- |     --[[...]]
-- |     r = main_payload()
-- |     rup1 = r1
-- |     --[[...]]
-- |   end
-- | end
-- | return test_f
-- Those `rn` variables before and after `main_payload` are
-- required to generate enough register pressure (for GPR). Amount
-- of repeats is empirical (e.g. 26 iterations).
-- Additional `test_f(...)` wrapper is needed for IR_VLOAD usage,
-- when `main_payload` is just `...`.
local function generate_payload(main_payload)
  local n_fillers = 26
  local code_chunk = 'local function test_f(...)\n'
  code_chunk = code_chunk .. 'local r\n'
  for i = 1, n_fillers do
    code_chunk = code_chunk .. ('local rup%d\n'):format(i)
  end

  code_chunk = code_chunk .. 'for _ = 1, 4 do\n'
  for i = 1, n_fillers do
    code_chunk = code_chunk ..
      ('local r%d = ffi.cast("int", %d)\n'):format(i, i)
  end
  code_chunk = code_chunk .. 'r = ' .. main_payload .. '\n'
  for i = 1, n_fillers do
    code_chunk = code_chunk .. ('rup%d = r%d\n'):format(i, i)
  end

  code_chunk = code_chunk .. 'end\nend\n'
  code_chunk = code_chunk .. 'return test_f'

  local f, err = loadstring(code_chunk, 'test_function')
  assert(type(f) == 'function', err)
  f = f()
  assert(type(f) == 'function', 'returned generated value is not a function')
  return f
end

-- Disable sink optimization to allocate more registers in a
-- "convenient" way. 'hotexit' option is required to be sure that
-- we will start a new trace on a false-positive guard assertion.
-- The new trace contains the same IR and so the same assertion
-- guard. This trace will be executed, the assertion guard failed
-- again and the new third trace will be recorded. This trace will
-- be the last one to record as far as iterations over cycle are
-- finished and we are returning from the function. The report of
-- `jit.dump` before the patch is the following:
-- | TRACE 1 start "test_function":29
-- | TRACE 1 stop -> loop
-- | TRACE 1 exit 0
-- | TRACE 2 start 1/0 "test_function":30
-- | TRACE 2 stop -> loop
-- | TRACE 2 exit 0
-- | TRACE 3 start 2/0 "test_function":30
-- | TRACE 3 stop -> return
-- On the other hand, after the patch we will have only 2 traces:
-- the main one, and the second is recorded on exit from loop.
-- Hence, the report of `jit.dump` will be the following:
-- | TRACE 1 start "test_function":29
-- | TRACE 1 stop -> loop
-- | TRACE 1 exit 3
-- | TRACE 2 start 1/3 "test_function":84
-- | TRACE 2 stop -> return
jit.opt.start('hotloop=1', 'hotexit=1', '-sink')
-- Prevent random hotcount to be sure that the cycle is compiled
-- first.
jit.off()
jit.flush()
for i = 1, N_TESTS do
  local f = generate_payload(TESTS[i].payload)
  jit.on()
  -- Argument is needed only for IR_VLOAD.
  f(true)
  jit.off()
  test:ok(not traceinfo(3), 'not recorded sidetrace for IR_' .. TESTS[i].irname)
  jit.flush()
end

test:done(true)
