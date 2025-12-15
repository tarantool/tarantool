local tap = require('tap')

-- Test file to demonstrate LuaJIT use-after-free in case of the
-- error in `loadfile()`.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1353.
local test = tap.test('lj-1353-loadfile-err-use-after-free'):skipcond({
  ['Too many GC objects on start'] = _TARANTOOL,
})

test:plan(1)

-- Determine the GC step size to finish the GC cycle in one step.
local full_step = 1
while true do
  collectgarbage('collect')
  collectgarbage('setpause', 0)
  collectgarbage('setstepmul', full_step)
  if collectgarbage('step') then break end
  full_step = full_step + 1
end

-- Check all possible GC step sizes.
for i = 1, full_step do
  collectgarbage('collect')
  collectgarbage('setpause', 0)
  collectgarbage('setstepmul', i)
  repeat
    -- On Linux-like systems this always returns `nil`, with the
    -- error: "cannot read .: Is a directory"
    -- The string for the filename "@." may be collected during
    -- the call, and later the pointer to the "." from that string
    -- is used after the string is free.
    loadfile('.')
  until collectgarbage('step')
end

test:ok(true, 'no use-after-free error')

test:done(true)
