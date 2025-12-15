local tap = require('tap')
local test = tap.test('gh-6098-fix-side-exit-patching-on-arm64'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

local generators = require('utils').jit.generators
local frontend = require('utils').frontend

test:plan(1)

-- Flush all possible traces and collect them to be sure that
-- we have enough space.
jit.flush()
collectgarbage()

-- The function to be tested for side exit patching:
-- * At the beginning of the test case, the <if> branch is
--   recorded as a root trace.
-- * After <refuncs> (and some other hotspots) are recorded, the
--   <else> branch is recorded as a side trace.
-- When JIT is linking the side trace to the corresponding side
-- exit, it patches the jump targets.
local function cbool(cond)
  if cond then
    return 1
  else
    return 0
  end
end

-- Make compiler work hard:
-- * No optimizations at all to produce more mcode.
-- * Try to compile all compiled paths as early as JIT can.
-- * Allow to compile 2Mb of mcode to be sure the issue occurs.
jit.opt.start(0, 'hotloop=1', 'hotexit=1', 'maxmcode=2048')

-- First call makes <cbool> hot enough to be recorded next time.
cbool(true)
-- Second call records <cbool> body (i.e. <if> branch). This is
-- a root trace for <cbool>.
cbool(true)

local cbool_traceno = frontend.gettraceno(cbool)

-- XXX: Unfortunately, we have no other option for extending
-- this jump delta, since the base of the current mcode area
-- (J->mcarea) is used as a hint for mcode allocator (see
-- lj_mcode.c for info).
generators.fillmcode(cbool_traceno, 1024 * 1024)

-- XXX: I tried to make the test in pure Lua, but I failed to
-- implement the robust solution. As a result I've implemented a
-- tiny Lua C API module to route the flow through C frames and
-- make JIT work the way I need to reproduce the fail. See the
-- usage below.
-- <pxcall> is just a wrapper for <lua_call> with "multiargs" and
-- "multiret" with the same signature as <pcall>.
local pxcall = require('libproxy').proxycall

-- XXX: Here is the dessert: JIT is aimed to work better for
-- highly biased code. It means, the root trace should be the
-- most popular flow. Furthermore, JIT also considers the fact,
-- that frequently taken side exits are *also* popular, and
-- compiles the side traces for such popular exits. However,
-- to recoup his attempts JIT try to compile the flow as far
-- as it can (see <lj_record_ret> in lj_record.c for more info).
--
-- Such "kind" behaviour occurs in our case: if one calls <cbool>
-- the native way, JIT continues recording in a lower frame after
-- returning from <cbool>. As a result, the second call is also
-- recorded, but it has to trigger the far jump to the side trace.
-- However, if the lower frame is not the Lua one, JIT doesn't
-- proceed the further flow recording and assembles the trace. In
-- this case, the second call jumps to <cbool> root trace, hits
-- the assertion guard and jumps to <cbool> side trace.
pxcall(cbool, false)
cbool(false)

test:ok(true)
test:done(true)
