local tap = require('tap')

-- Test file to demonstrate incorrect Lua stack restoration on
-- exit from trace by the stack overflow.

local test = tap.test('fix-stack-alloc-on-trace-exit'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local jit_dump = require('jit.dump')

test:plan(2)

-- Before the patch, it is possible that a snapshot topslot is
-- less than the possible topslot of the Lua stack. In that case,
-- if the Lua stack overflows in `lj_vmevent_prepare()`, the error
-- is raised inside `lj_vm_exit_handler()`, which has no
-- corresponding DWARF eh_frame, so it leads to the crash.

-- Need for the stack growing in `lj_vmevent_prepare`.
jit_dump.start('x', '/dev/null')

-- Create a coroutine with a fixed stack size.
local coro = coroutine.create(function()
  jit.opt.start('hotloop=1', 'hotexit=1', 'callunroll=1')

  -- `math.modf` recording is NYI.
  -- Local `math_modf` simplifies `jit.dump()` output.
  local math_modf = math.modf

  local function trace(n)
    n = n + 1
    -- luacheck: ignore
    -- Start a side trace here.
    if n % 2 == 0 then end
    -- Stop the recording of the side trace and a main trace,
    -- stitching.
    math_modf(1, 1)
    -- Grow stack, avoid tail calls.
    local unused = trace(n)
    return unused
  end

  local n = 0
  trace(n)
end)

local result, errmsg = coroutine.resume(coro)

test:ok(not result, 'correct status and no crash')
test:like(errmsg, 'stack overflow', 'correct error message')

test:done(true)
