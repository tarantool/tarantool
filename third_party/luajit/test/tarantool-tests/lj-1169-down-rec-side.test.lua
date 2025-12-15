local tap = require('tap')

-- Test file to demonstrate the LuaJIT misbehaviour when recording
-- and executing a side trace with down-recursion.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1169.

local test = tap.test('lj-1169-down-rec-side'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local ffi = require('ffi')

-- XXX: simplify `jit.dump()` output.
local modf = math.modf
local ffi_new = ffi.new

local int64_t = ffi.typeof('int64_t')

test:plan(1)

-- If a parent trace has more than the default amount of spill
-- slots, the `rsp` register is adjusted at the start of the trace
-- and restored after. If there is a side trace created, it
-- modifies the stack only at exit (since adjustment is inherited
-- from a parent trace). If the side trace has a down-recursion
-- (for now only the down-recursion to itself is used), `rsp` may
-- be modified several times before exit, so the host stack
-- becomes corrupted.
--
-- This test provides the example of a side trace (5) with
-- down-recursion.

local function trace_ret(arg) -- TRACE 1 start.
  return arg -- TRACE 4 start 1/0; TRACE 5 start 4/0.
end

local function extra_frame()
  -- Stitch the trace (4) to prevent early down-recursion.
  modf(1)
  -- Start the side trace (5) with a down-recursion.
  return trace_ret({})
end

local call_counter = 0
local function recursive_f() -- TRACE 2 start.
  -- XXX: 4 calls are needed to record the necessary traces after
  -- return. With the 5th call, the traces are executed.
  if call_counter > 4 then return end -- TRACE 3 start 2/1.
  call_counter = call_counter + 1

  -- Compile the root trace first.
  trace_ret(1)
  trace_ret(1)

  recursive_f()
  -- Stop the side trace (3) here after exiting the trace (2) with
  -- up-recursion.
  modf(1)

  -- Start the side trace (4).
  trace_ret('')
  -- luacheck: no unused
  -- Generate register pressure to force spills.
  -- The amount is well-suited for arm64 and x86_64.
  local l1 = ffi_new(int64_t, call_counter + 1)
  local l2 = ffi_new(int64_t, call_counter + 2)
  local l3 = ffi_new(int64_t, call_counter + 3)
  local l4 = ffi_new(int64_t, call_counter + 4)
  local l5 = ffi_new(int64_t, call_counter + 5)
  local l6 = ffi_new(int64_t, call_counter + 6)
  local l7 = ffi_new(int64_t, call_counter + 7)
  local l8 = ffi_new(int64_t, call_counter + 8)
  local l9 = ffi_new(int64_t, call_counter + 9)
  local l10 = ffi_new(int64_t, call_counter + 10)
  local l11 = ffi_new(int64_t, call_counter + 11)
  local l12 = ffi_new(int64_t, call_counter + 12)
  local l13 = ffi_new(int64_t, call_counter + 13)
  -- Simulate the return to the same function using the additional
  -- frame for down-recursion.
  return trace_ret(extra_frame())
end

jit.opt.start('hotloop=1', 'hotexit=1', 'recunroll=1')

recursive_f()

test:ok(true, 'no crash during execution of a trace with down-recursion')

test:done(true)
