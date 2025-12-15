local hotloop = assert(arg[1], 'hotloop argument is missing')
local trigger = assert(arg[2], 'trigger argument is missing')

local ffi = require('ffi')
local ffisandwich = ffi.load('libsandwich')
ffi.cdef('int increment(struct sandwich *state, int i)')

-- Save the current coroutine and set the value to trigger
-- <increment> call the Lua routine instead of C implementation.
local sandwich = require('libsandwich')(trigger)

-- Depending on trigger and hotloop values the following contexts
-- are possible:
-- * if trigger <= hotloop -> trace recording is aborted
-- * if trigger >  hotloop -> trace is recorded but execution
--   leads to panic
jit.opt.start(string.format('hotloop=%d', hotloop))

local res
for i = 0, hotloop + trigger do
  res = ffisandwich.increment(sandwich, i)
end
-- Check the resulting value if panic didn't occur earlier.
assert(res == hotloop + trigger + 1, 'res is calculated correctly')
io.write('#4427 still works')
