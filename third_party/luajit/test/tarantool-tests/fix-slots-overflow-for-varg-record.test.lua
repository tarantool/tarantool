local tap = require('tap')

-- Test file to demonstrate `J->slots` buffer overflow when
-- recording the `BC_VARG` bytecode.

local test = tap.test('fix-slots-overflow-for-varg-record'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

test:plan(1)

-- Before the patch, the JIT compiler checked the slots overflow
-- for recording of the `BC_VARG` bytecode too late, when the
-- overflow of the `slot` buffer had already occurred. Hence, some
-- fields of the `jit_State` structure (see <src/lj_jit.h> for
-- details) may be overwritten. Unfortunately, neither UBSAN, nor
-- ASAN, nor Valgrind can catch such misbehaviour for now. So we
-- should observe the results of overwritten fields in the
-- structure.
--
-- In particular, the content of the `params` array with the JIT
-- engine settings is corrupted. One of the options to be
-- overwritten is the `maxirconst` that overflows, so no trace can
-- be compiled. An attempt to compile any trace will fail.
--
-- The test fails before the commit on the GC64 build and may lead
-- to the core dump for the non-GC64 build.

local traceinfo = require('jit.util').traceinfo

-- XXX: Use a vararg function here to prevent its compilation
-- after failing recording for the main trace.
-- luacheck: no unused
local function empty(...)
end

local function varg_with_slot_overflow(...)
  -- Try to record `BC_VARG`. It should fail due to slots overflow
  -- when trying to copy varargs from slots of the incoming
  -- parameters to slots of arguments for the call.
  empty(...)
end
_G.varg_with_slot_overflow = varg_with_slot_overflow

-- Prevent the compilation of unrelated traces.
jit.off()
jit.flush()

-- XXX: Generate the function with the maximum possible (to
-- preserve JIT compilation of the call) arguments given to the
-- vararg function to call. Use sizings for the GC64 mode since it
-- occupies more slots for the frame.
-- Reproduce is still valid for non-GC64 mode since the difference
-- is only several additional slots and buffer overflow is still
-- observed.
local LJ_MAX_JSLOTS = 250
-- Amount of slots occupied for a call of a vararg function for
-- GC64 mode.
local MAX_VARG_FRAME_SZ = 4
-- `J->baseslot` at the start of the recording of the call to the
-- vararg function for GC64 mode.
local MAX_BASESLOT = 8
-- The maximum possible number of slots to occupy is
-- `LJ_MAX_JSLOTS` without:
-- * `J->baseslot` offset at the moment of the call,
-- * 2 vararg frames,
-- * 2 slots for the functions to call.
local chunk = 'varg_with_slot_overflow(1'
for i = 2, LJ_MAX_JSLOTS - MAX_BASESLOT - 2 * MAX_VARG_FRAME_SZ - 2 do
  chunk = chunk .. (', %d'):format(i)
end
chunk = chunk .. ')'

local caller_of_varg = assert(loadstring(chunk))

-- Use an additional frame to simplify the `J->baseslot`
-- calculation.
local function wrapper()
  for _ = 1, 4 do
    caller_of_varg()
  end
end

jit.on()
jit.opt.start('hotloop=1')

wrapper()

assert(not traceinfo(1), 'no traces recorded')

-- Reset hot counters to avoid collisions and blacklisting.
jit.opt.start('hotloop=1')

-- The simplest trace to compile.
for _ = 1, 4 do end

jit.off()

test:ok(traceinfo(1), 'trace is recorded, so structure is not corrupted')

test:done(true)
