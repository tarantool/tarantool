local tap = require('tap')
local test = tap.test('lj-1046-fix-bc-varg-recording'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

jit.opt.start('hotloop=1')

-- luacheck: ignore
local anchor
local N_ITER = 5
local SIDE_ITER = N_ITER - 1

local consistent_snap_restore = false

for i = 1, N_ITER do
  -- This trace generates the following IRs:
  -- 0001 >  int SLOAD  #7    CRI
  -- 0002 >  int LE     0001  +2147483646
  -- 0003    int SLOAD  #6    CI
  -- 0004    int SLOAD  #0    FR
  -- 0005 >  int LE     0004  +11
  -- 0006 >  num SLOAD  #5    T
  -- 0007    num CONV   0003  num.int
  -- ....        SNAP   #1   [ ---- ---- ---- nil  ]
  -- 0008 >  num ULE    0007  0006
  -- 0009  + int ADD    0003  +1
  -- ....        SNAP   #2   [ ---- ---- ---- nil  ---- ---- ]
  -- 0010 >  int LE     0009  0001
  -- ....        SNAP   #3   [ ---- ---- ---- nil  ---- ---- 0009 0001 ---- 0009 ]
  -- 0011 ------ LOOP ------------
  -- 0012    num CONV   0009  num.int
  -- ....        SNAP   #4   [ ---- ---- ---- nil  ]
  --
  -- In case, when `BC_VARG` sets the VARG slot to the non-top
  -- stack slot, `maxslot` value was unconditionally set to the
  -- destination slot, so the following snapshot (same for the #1)
  -- is used:
  -- ....        SNAP   #4   [ ---- ---- ---- nil  ]
  -- instead of:
  -- ....        SNAP   #4   [ ---- ---- ---- nil  ---- ---- 0009 0001 ---- 0009 ]
  -- Since these slots are omitted, they are not restored
  -- correctly, when restoring from the snapshot for this side
  -- exit.
  anchor = ...
  if i > SIDE_ITER then
    -- XXX: `test:ok()` isn't used here to avoid double-running of
    -- tests in case of `i` incorrect restoration from the
    -- snapshot.
    consistent_snap_restore = i > SIDE_ITER
    break
  end
end

test:ok(consistent_snap_restore, 'BC_VARG recording 0th frame depth, 1 result')

-- Now the same case, but with an additional frame, so VARG slots
-- are defined on the trace.
local function varg_frame(anchor, i, side_iter, ...)
  anchor = ...
  if i > side_iter then
    -- XXX: `test:ok()` isn't used here to avoid double-running of
    -- tests in case of `i` incorrect restoration from the
    -- snapshot.
    consistent_snap_restore = i > side_iter
  end
end

consistent_snap_restore = false

for i = 1, N_ITER do
  varg_frame(nil, i, SIDE_ITER)
end

test:ok(consistent_snap_restore,
        'BC_VARG recording with VARG slots defined on trace, 1 result')

test:done(true)
