local tap = require('tap')
local profile = require('jit.profile')

local test = tap.test('lj-512-profiler-hook-finalizers'):skipcond({
  -- Disable the test since it is time-sensitive.
  ['Disabled with Valgrind'] = os.getenv('LUAJIT_TEST_USE_VALGRIND'),
})
test:plan(1)

-- Sampling interval in ms.
local INTERVAL = 10

local nsamples = 0
profile.start('li'..tostring(INTERVAL), function()
  nsamples = nsamples + 1
end)

local start = os.clock()
for _ = 1, 1e6 do
   getmetatable(newproxy(true)).__gc = function() end
end
local finish = os.clock()

profile.stop()

-- XXX: The bug is occurred as stopping of callbacks invocation,
-- when a new tick strikes inside `gc_call_finalizer()`.
-- The amount of successful callbacks isn't stable (2-15).
-- So, assume that amount of profiling samples should be at least
-- more than 0.5 intervals of time during sampling.
test:ok(nsamples >= 0.5 * (finish - start) * 1e3 / INTERVAL,
        'profiler sampling')

test:done(true)
