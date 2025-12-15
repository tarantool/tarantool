local tap = require('tap')
local test = tap.test('lj-1066-fix-cur_L-after-coroutine-resume'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local libcur_L_coroutine = require('libcur_L_coroutine')

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

local res = pcall(libcur_L_coroutine.error_after_coroutine_return)
assert(res == false, 'return from error')
-- Call with restoration from a snapshot with wrong cur_L.
cbool(false)

test:ok(true)
test:done(true)
