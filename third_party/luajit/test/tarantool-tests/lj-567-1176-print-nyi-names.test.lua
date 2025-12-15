local tap = require('tap')

-- Test dumping of names of NYI bytecodes to be compiled instead
-- of their numbers. See also:
-- * https://github.com/LuaJIT/LuaJIT/pull/567,
-- * https://github.com/LuaJIT/LuaJIT/issues/1176.

local test = tap.test('lj-567-1176-print-nyi-names'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

local jparse = require('utils').jit.parse

test:plan(3)

local function reset_jit()
  -- Remove all previous traces.
  jit.off()
  jit.flush()
  jit.on()
  jit.opt.start('hotloop=1')
end

local function nop() end
local function test_varg(...)
  for _ = 1, 4 do nop(...) end
end

local function test_fnew()
  for _ = 1, 4 do
    local _ = function() end
  end
end

local function test_uv()
  do
    -- luacheck: ignore
    local uclo
    goto close_uv
    local function _()
      return uclo
    end
  end
  ::close_uv::
end

-- We only need the abort reason in the test.
jparse.start('t')
reset_jit()

test_varg()

local _, aborted_traces = jparse.finish()
assert(aborted_traces and aborted_traces[1],
       'aborted trace with VARG is persisted')

-- We tried to compile only one trace.
local reason = aborted_traces[1][1].abort_reason
test:like(reason, 'NYI: bytecode VARG', 'bytecode VARG name')

jparse.start('t')
reset_jit()

test_fnew()

_, aborted_traces = jparse.finish()
assert(aborted_traces and aborted_traces[1],
       'aborted trace with FNEW is persisted')

reason = aborted_traces[1][1].abort_reason
test:like(reason, 'NYI: bytecode FNEW', 'bytecode FNEW name')

jparse.start('t')
reset_jit()

test_uv()
test_uv()

_, aborted_traces = jparse.finish()
assert(aborted_traces and aborted_traces[1],
       'aborted trace with UCLO is persisted')

reason = aborted_traces[1][1].abort_reason
test:like(reason, 'NYI: bytecode UCLO', 'bytecode UCLO name')

test:done(true)
