local tap = require('tap')

-- Test file to demonstrate LuaJIT crash during snapshot restore
-- in case of the stack overflow.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1196.

local test = tap.test('lj-1196-partial-snap-restore')
test:plan(1)

-- XXX: The reproducer below uses several stack slot offsets to
-- make sure that stack overflow happens during the snapshot
-- restoration and not the call to the stitched function and
-- return its result. The actual stack size should be less than
-- `LJ_STACK_MAXEX`, but with requested space it should be greater
-- than `LJ_STACK_MAX`, see <src/lj_state.c> for the details.
-- Before that, the `lj_snap_restore()` restores the `pc` for the
-- inner cframe, but not the outer (before the protected call to
-- the `trace_exit_cp()`). Thus, the further error rethrowing from
-- the right C frame leads to the crash before the patch.

-- XXX: Simplify the `jit.dump()` output.
local tonumber = tonumber

-- This function starts the first trace.
local function recursive_f()
  -- Function with the single result to cause the trace stitching.
  tonumber('')
  -- Prereserved stack space before the call.
  -- luacheck: no unused
  local _, _, _, _, _, _, _, _, _, _, _
  -- Link from the stitched trace to the parent one.
  recursive_f()
  -- Additional stack space required for the snapshot restoration.
  -- luacheck: no unused
  local _, _, _
end

-- Use coroutine wrap for the fixed stack size at the start.
coroutine.wrap(function()
  -- XXX: Special stack slot offset.
  -- luacheck: no unused
  local _, _, _, _, _, _, _, _, _, _
  -- The error is observed only if we have the error handler set,
  -- since we try to resize stack for its call.
  xpcall(recursive_f, function() end)
end)()

test:ok(true, 'no crash during snapshot restoring')

test:done(true)
