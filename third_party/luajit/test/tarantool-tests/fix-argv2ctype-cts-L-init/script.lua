local ffi = require('ffi')

jit.opt.start('hotloop=1')

local i = 1
-- Use `while` to start compilation of the trace at the first
-- iteration, before `ffi.new()` is called, so `cts->L` is
-- uninitialized.
while i < 3 do
  ffi.new('uint64_t', i)
  i = i + 1
end

print('OK')
