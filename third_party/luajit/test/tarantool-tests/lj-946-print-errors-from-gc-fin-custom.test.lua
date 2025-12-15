local tap = require('tap')
local test = tap.test('lj-946-print-errors-from-gc-fin-custom'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

local ffi = require('ffi')
local error_in_finalizer = false

local function errfin_handler()
    error_in_finalizer = true
end

local function new_bad_cdata()
  return ffi.gc(ffi.new('char [?]', 1024), 'uncallable string')
end

local function test_f()
  collectgarbage('collect')
  -- Make GC aggressive enough to end the atomic phase before
  -- exiting the trace.
  collectgarbage('setstepmul', 400)
  -- The number of iterations is empirical, just big enough for
  -- the issue to strike.
  for _ = 1, 10000 do
    new_bad_cdata()
  end
end

jit.opt.start('hotloop=1')
-- Handler is registered but never called before the patch.
-- It should be called after the patch.
jit.attach(errfin_handler, 'errfin')
local status = pcall(test_f)
-- We have to stop GC now because any step raises the error due to
-- cursed cdata objects.
collectgarbage('stop')
test:ok(status, 'test function completed successfully')
test:ok(error_in_finalizer, 'error handler called')

test:done(true)
