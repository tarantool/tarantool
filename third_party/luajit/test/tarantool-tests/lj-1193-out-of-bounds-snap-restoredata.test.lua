local tap = require('tap')

-- Test file to demonstrate LuaJIT's out-of-bounds access during
-- the saving of registers content in `snap_restoredata()`.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1193.

local test = tap.test('lj-1193-out-of-bounds-snap-restoredata'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local ffi = require('ffi')

test:plan(1)

local double_type = ffi.typeof('double')

jit.opt.start('hotloop=1')
local x = 1LL
for _ = 1, 4 do
  -- `x` is saved in the fpr register and will be restored in the
  -- `ex->fpr` during exit from the snapshot. But out-of-bounds
  -- access is happening due to indexing `ex->gpr` occasionally.
  x = double_type(x + 1)
end

test:ok(true, 'no out-of-bounds failure')

test:done(true)
