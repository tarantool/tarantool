local hotloop = assert(arg[1], 'hotloop argument is missing')
local trigger = assert(arg[2], 'trigger argument is missing')

local ffi = require('ffi')
local ffiflush = ffi.load('libflush')
ffi.cdef('void flush(struct flush *state, int i)')

-- Save the current coroutine and set the value to trigger
-- <flush> call the Lua routine instead of C implementation.
local flush = require('libflush')(trigger)

-- Depending on trigger and hotloop values the following contexts
-- are possible:
-- * if trigger <= hotloop -> trace recording is aborted
-- * if trigger >  hotloop -> trace is recorded but execution
--   leads to panic
jit.opt.start(string.format('hotloop=%d', hotloop))

for i = 0, trigger + hotloop do
  ffiflush.flush(flush, i)
end
-- Panic didn't occur earlier.
io.write('LJ flush still works')
