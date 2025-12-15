local tap = require('tap')

-- Test file to demonstrate LuaJIT's incorrect Scalar Evolution
-- analysis for recording of return to a lower frame.
-- See also: https://github.com/LuaJIT/LuaJIT/pull/1115.
local test = tap.test('lj-1115-invalid-scev-entry-lower-frame'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local HOTLOOP = 1
local HOTEXIT = 1
local RECORD_IDX = HOTLOOP + 1
-- Number of iterations to start recording side trace with two
-- iterations in the cycle.
local NITER = RECORD_IDX + HOTEXIT + 2

local function test_function(tab)
  -- XXX: For reproducing the issue, it is necessary to avoid
  -- UGET. Local functions use MOV and take the same IR slots.
  local function trace_root(data)
    -- Start of the trace, setup ScEv entry.
    for i = 1, #data - 1 do
      -- Start of the side trace by the hmask check.
      if data[i].t == 'a' then
        return i + 1
      end
    end
    -- Unreachable in this test.
    return nil
  end

  local function other_scev(data, start)
    for i = start, #data - 1 do
      -- The ScEv entry matches the recorded IR from the parent
      -- trace before the patch. It leads to the assertion
      -- failure.
      if data[i].t == 'a' then
        return
      end
    end
  end

  -- Record the root trace first. Then record the side trace
  -- returning to the lower frame (this function).
  local start = trace_root(tab)
  -- The ScEv entry is invalid after the return to the lower
  -- frame. Record the trace with another range in the ScEv entry
  -- to obtain the error.
  return start, other_scev(tab, start)
end

local data = {}
for i = 1, NITER do
  data[#data + 1] = {t = 'a' .. i}
end

-- Change the hmask value to start the side trace recording.
data[RECORD_IDX] = {}
-- Setup for the trace's return to the lower frame.
data[NITER - 2] = {t = 'a'}

jit.opt.start('hotloop=' .. HOTLOOP, 'hotexit=' .. HOTEXIT)

test_function(data)

test:ok(true, 'correct ScEv entry invalidation for return to a lower frame')
test:done(true)
