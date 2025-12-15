local tap = require('tap')

-- The test file to demonstrate the incorrect JIT behaviour during
-- OOM on the finalizer table rehashing in the context of the JIT
-- trace.
-- See also:
-- * https://github.com/LuaJIT/LuaJIT/issues/1247,
-- * https://github.com/tarantool/tarantool/issues/10290.

local test = tap.test('lj-1247-fin-tab-rehashing-on-trace'):skipcond({
  ['Broken unwiding in tarantool_panic_handler'] = _TARANTOOL and
                                                   (jit.os == 'OSX'),
  ['Disabled on MacOS due to #8652'] = jit.os == 'OSX',
  ['Test requires JIT enabled'] = not jit.status(),
})

-- XXX: The original issue has 2 ways to crash:
-- 1) in `lj_trace_unwind()`
-- 2) in `lj_trace_exit()`
-- But, since we have an additional GC pressure due to requiring a
-- `tap` module, the second case needs an impossibly big
-- `gcstepmul` value to reproduce the issue. So, since the root
-- issue is the same and now rehashing of finalizer table is
-- omitted, we test only the first case.
test:plan(2)

local allocinject = require('allocinject')

local ffi = require('ffi')
ffi.cdef[[
  struct test {int a;};
]]

local N_GC_STEPS = 100
local N_GC_FINALIZERS = 100

local function empty() end

-- Create a chunk like the following:
--[[
  local tostring = tostring
  local r = ...
  for _ = 1, 4 do
    r[1] = tostring(1)
    -- ...
    r[N_GCSTEPS] = tostring(N_GC_STEPS)
  end
--]]
local function create_chunk(n_steps)
  local chunk = 'local tostring = tostring\n'
  chunk = chunk .. ('local r = ...\n')
  chunk = chunk .. 'for _ = 1, 4 do\n'
  for i = 1, n_steps do
    chunk = chunk .. ('  r[%d] = tostring(%d)\n'):format(i, i)
  end
  chunk = chunk .. 'end\n'
  chunk = chunk .. 'return r\n'
  return chunk
end

local function add_more_garbage(size)
  return ffi.new('char[?]', size)
end

-- Helper to skip the atomic phase.
local function skip_atomic()
  local first_gc_called = false
  local function mark_fin() first_gc_called = true end
  jit.off(mark_fin)
  debug.getmetatable(newproxy(true)).__gc = mark_fin

  -- Skip the atomic phase.
  jit.off()
  while not first_gc_called do collectgarbage('step') end
  jit.on()
end

local function crash_on_trace_unwind_gc_setup()
  skip_atomic()
  collectgarbage('setstepmul', 1000)
  add_more_garbage(1024 * 1024)
end

local f = assert(loadstring(create_chunk(N_GC_STEPS)))

-- Create a really long trace.
jit.flush()
jit.opt.start('hotloop=2', 'maxirconst=5000', 'maxrecord=10000', 'maxsnap=1000',
              '-fold')

-- luacheck: no unused
local gc_anchor = {}
local function anchor_finalizer(i)
  gc_anchor[i] = ffi.gc(ffi.new('struct test', i), empty)
end

for i = 1, N_GC_FINALIZERS do
  anchor_finalizer(i)
end

-- Record the trace first.
f({})

-- The table for anchoring cdata objects.
local res_tab = {}

collectgarbage()
collectgarbage()
collectgarbage('setpause', 0)
collectgarbage('setstepmul', 1)

gc_anchor = nil

crash_on_trace_unwind_gc_setup()

-- OOM on every allocation (i.e., on finalizer table rehashing
-- too).
allocinject.enable_null_alloc()

local r, err = pcall(f, res_tab)

allocinject.disable()

test:ok(not r, 'correct status')
test:like(err, 'not enough memory', 'correct error message')

test:done(true)
