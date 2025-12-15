local tap = require('tap')

-- Test file to demonstrate LuaJIT incorrect recording of `TSETM`
-- bytecode.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1025.

local test = tap.test('lj-1025-tsetm-maxslot'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local jit_dump = require('jit.dump')

local TEST_VALUE = '5'
local TEST_IDX = 5

-- XXX: Use a big enough slot number to be overwritten by the VM
-- event handler function. This value is empirical.
local function slot5()
  return nil, nil, nil, nil, TEST_VALUE
end

local storage
local function test_tsetm(...)
  -- Usage of `TSETM` bytecode.
  storage = {slot5()}
  -- Use this function again to trick use-def analysis and avoid
  -- cleaning JIT slots, so the last JIT slot contains
  -- `TEST_VALUE`.
  return slot5(...)
end

-- Wrapper to avoid the recording of just the inner `slot5()`
-- function.
local function wrap()
  test_tsetm()
end

jit.opt.start('hotloop=1')
-- We need to call the VM event handler after each recorded
-- bytecode instruction to pollute the Lua stack and make the
-- issue observable.
jit_dump.start('b', '/dev/null')

-- Compile and execute the trace with `TSETM`.
wrap()
wrap()
wrap()

test:is(storage[TEST_IDX], TEST_VALUE,
        'BC_TSETM recording with enabled jit.dump')

test:done(true)
