local ffi = require('ffi')

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
local status = pcall(test_f)
-- We have to stop GC now because any step raises the error due to
-- cursed cdata objects.
collectgarbage('stop')
assert(status, 'error is not rethrown')
