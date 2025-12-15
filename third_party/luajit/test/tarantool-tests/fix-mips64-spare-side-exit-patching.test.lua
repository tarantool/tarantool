local tap = require('tap')
local test = tap.test('fix-mips64-spare-side-exit-patching'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
  -- We need to fix the MIPS behaviour first.
  ['Disabled for MIPS architectures'] = jit.arch:match('mips'),
})

local generators = require('utils').jit.generators
local frontend = require('utils').frontend
local jutil = require('jit.util')

-- Allow compilation of up to 2000 traces to avoid flushes.
local MAXTRACE = 2000;

test:plan(1)

-- Flush all possible traces and collect them to be sure that
-- we have enough space.
jit.flush()
collectgarbage()

local function find_last_trace()
  local candidate = misc.getmetrics().jit_trace_num
  for traceno = candidate, MAXTRACE do
    -- There is no need for heavy calls here. Just use the
    -- simplest one to invoke `lj_checktrace()`.
    if jutil.tracemc(traceno) then
      candidate = traceno
    end
  end
  assert(jutil.tracemc(candidate), 'tracenum candidate is invalid')
  return candidate
end

-- Make compiler work hard.
jit.opt.start(
  -- No optimizations at all to produce more mcode.
  0,
  -- Try to compile all compiled paths as early as JIT can.
  'hotloop=1',
  'hotexit=1',
  ('maxtrace=%d'):format(MAXTRACE),
  -- Allow to compile 8Mb of mcode to be sure the issue occurs.
  'maxmcode=8192',
  -- Use big mcode area for traces to avoid usage of different
  -- spare slots.
  'sizemcode=256'
)

-- See the define in the <src/lj_asm_mips.h>.
local MAX_SPARE_SLOT = 4
local function parent(marker)
  -- Use several side exits to fill spare exit space (default is
  -- 4 slots, each slot has 2 instructions -- jump and nop).
  -- luacheck: ignore
  if marker > MAX_SPARE_SLOT then end
  if marker > 3 then end
  if marker > 2 then end
  if marker > 1 then end
  if marker > 0 then end
  -- XXX: use `fmod()` to avoid leaving the function and use
  -- stitching here.
  return math.fmod(1, 1)
end

-- Compile parent trace first.
parent(0)
parent(0)

local parent_traceno = frontend.gettraceno(parent)
local last_traceno = parent_traceno

-- Now generate some mcode to forcify long jump with a spare slot.
-- Each iteration provides different addresses and uses a
-- different spare slot. After that, compiles and executes a new
-- side trace.
for i = 1, MAX_SPARE_SLOT + 1 do
  generators.fillmcode(last_traceno, 1024 * 1024)
  parent(i)
  parent(i)
  parent(i)
  last_traceno = find_last_trace()
end

test:ok(true, 'all traces executed correctly')

test:done(true)
