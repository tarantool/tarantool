local tap = require('tap')
local test = tap.test('gh-7264-add-proto-trace-sysprof-default'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Sysprof is implemented for x86_64 only'] = jit.arch ~= 'x86' and
                                               jit.arch ~= 'x64',
  ['Sysprof is implemented for Linux only'] = jit.os ~= 'Linux',
  -- See also https://github.com/LuaJIT/LuaJIT/issues/606.
  ['Disabled due to LuaJIT/LuaJIT#606'] = os.getenv('LUAJIT_TABLE_BUMP'),
  ['Sysprof is disabled'] = os.getenv('LUAJIT_DISABLE_SYSPROF'),
  -- See also https://github.com/tarantool/tarantool/issues/10803.
  ['Disabled due to #10803'] = os.getenv('LUAJIT_TEST_USE_VALGRIND'),
})

test:plan(2)

local chunk = [[
return function()
  local a = 'teststring'
end
]]

local function allocate()
  local a = {}
  for _ = 1, 3 do
    table.insert(a, 'teststring')
  end
  return a
end

-- Proto creation during the sysprof runtime.
jit.off()

assert(misc.sysprof.start({ mode = 'D' }))
-- The first call yields the anonymous function created by loading
-- <chunk> proto. As a result the child proto function is yielded.
-- The second call invokes the child proto function to trigger
-- <lj_sysprof_add_proto> call.
assert(load(chunk))()()
test:ok(misc.sysprof.stop(), 'new proto in sysprof runtime')

-- Trace creation during the sysprof runtime.
jit.flush()
jit.opt.start('hotloop=1')
jit.on()

assert(misc.sysprof.start({ mode = 'D' }))
-- Run <allocate> function to record a new trace. As a result,
-- <lj_sysprof_add_trace> is triggered to be invoked.
allocate()
test:ok(misc.sysprof.stop(), 'trace record in sysprof runtime')

test:done(true)
