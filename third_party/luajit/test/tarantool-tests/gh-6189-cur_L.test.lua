local tap = require('tap')
local test = tap.test('gh-6189-cur_L'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local libcur_L = require('libcur_L')

local function cbool(cond)
  if cond then
    return 1
  else
    return 0
  end
end

-- Compile function to trace with snapshot.
jit.opt.start('hotloop=1')
-- First call makes `cbool()` hot enough to be recorded next time.
cbool(true)
-- Second call records `cbool()` body (i.e. `if` branch). This is
-- a root trace for `cbool()`.
cbool(true)

assert(pcall(libcur_L.error_from_other_thread) == false, "return from error")
-- Call with restoration from a snapshot with wrong cur_L.
cbool(false)

test:ok(true)
test:done(true)
