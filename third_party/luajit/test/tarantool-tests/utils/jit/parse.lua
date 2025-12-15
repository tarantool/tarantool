local M = {}

local jdump = require('jit.dump')

local trace_mt = {}
trace_mt.__index = trace_mt

-- Find and return the first IR ref and value matched the pattern
-- for the trace.
trace_mt.has_ir = function(trace, ir_pattern)
  for i = 1, #trace.ir do
    local ir = trace.ir[i]
    if ir:match(ir_pattern) then
      return i, ir
    end
  end
end

local function trace_new(n)
  return setmetatable({
    number = n,
    parent = nil,
    parent_exitno = nil,
    is_stitched = false,
    abort_reason = nil,
    start_loc = nil,
    bc = {},
    ir = {},
    mcode = {},
    snaps = {},
  }, trace_mt)
end

local function parse_bc(trace, line)
  trace.bc[#trace.bc + 1] = line
end

local function parse_snap(trace, n_snap, line)
  assert(trace.snaps[n_snap] == nil)
  trace[n_snap] = {ref = #trace.ir + 1, slots = line:match('%[(.*)%]$'),}
end

local function parse_ir(trace, line)
  local n_snap = line:match('SNAP%s+#(%d+)')
  if n_snap then
    parse_snap(trace, n_snap, line)
    return
  end

  local current_ins, ir = line:match('^(%d+)%s+(.*)$')
  current_ins = tonumber(current_ins)
  -- Insert NOP instruction hidden in IR dump.
  if current_ins ~= #trace.ir + 1 then
    trace.ir[#trace.ir + 1] = 'nil NOP'
  end
  assert(current_ins == #trace.ir + 1)
  trace.ir[current_ins] = ir
end

local function parse_mcode(trace, line)
  -- Skip loop label.
  if line == '-> LOOP:' then
    return
  end
  local addr, instruction = line:match('^(%w+)%s+(.*)$')
  trace.mcode[#trace.mcode + 1] = {addr = addr, instruction = instruction,}
end

local header_handlers = {
  start = function(ctx, trace_num, line)
    local traces = ctx.traces
    local trace = trace_new(trace_num)
    if line:match('start %d+/stitch') then
      trace.parent = line:match('start (%d+)/stitch')
      trace.is_stitched = true
    else
      trace.parent, trace.parent_exitno = line:match('start (%d+)/(%d+)')
    end
    -- XXX: Assume, that source line can't contain spaces.
    -- For example, it's not "(command line)".
    trace.start_loc = line:match(' ([^%s]+)$')
    traces[trace_num] = trace
    ctx.parsing_trace = trace_num
    ctx.parsing = 'bc'
  end,
  stop = function(ctx, trace_num)
    assert(ctx.parsing_trace == trace_num)
    ctx.parsing_trace = nil
    ctx.parsing = nil
  end,
  abort = function(ctx, trace_num, line)
    local traces = ctx.traces
    assert(ctx.parsing_trace == trace_num)

    local aborted_traces = ctx.aborted_traces
    if not aborted_traces[trace_num] then
      aborted_traces[trace_num] = {}
    end
    -- The reason is mentioned after "-- " at the end of the
    -- string.
    traces[trace_num].abort_reason = line:match('-- (.+)$')
    table.insert(aborted_traces[trace_num], traces[trace_num])

    ctx.parsing_trace = nil
    ctx.parsing = nil
    traces[trace_num] = nil
  end,
  IR = function(ctx)
    ctx.parsing = 'IR'
  end,
  mcode = function(ctx)
    ctx.parsing = 'mcode'
  end,
}

local function parse_line(ctx, line)
  if line == '' then
    return
  end

  if line:match('TRACE flush') then
    ctx.traces = {}
    return
  end

  local trace_num, status = line:match('TRACE (%d+) (%w+)')
  if trace_num then
    if (header_handlers[status]) then
       header_handlers[status](ctx, tonumber(trace_num), line)
    else
      error('Unknown trace status: ' .. status)
    end
    return
  end

  assert(ctx.parsing_trace)

  local trace = ctx.traces[ctx.parsing_trace]
  if ctx.parsing == 'bc' then
    parse_bc(trace, line)
  elseif ctx.parsing == 'IR' then
    parse_ir(trace, line)
  elseif ctx.parsing == 'mcode' then
    parse_mcode(trace, line)
  end
end

local JDUMP_FILE

local function parse_jit_dump()
  local ctx = {
    aborted_traces = {},
    traces = {},
  }
  for line in io.lines(JDUMP_FILE) do
    parse_line(ctx, line)
  end
  return ctx.traces, ctx.aborted_traces
end

-- Start `jit.dump()` utility with the given flags, saving the
-- output in a temporary file.
M.start = function(flags)
  assert(JDUMP_FILE == nil, 'jit_parse is already running')
  -- Always use plain text output.
  flags = flags .. 'T'
  -- Turn off traces compilation for `jit.dump` to avoid side
  -- effects for the period of the testing.
  jit.off(jdump.on, true)
  jit.off(jdump.off, true)
  JDUMP_FILE = os.tmpname()
  jdump.start(flags, JDUMP_FILE)
end

-- Stop `jit.dump()` utility parsing the output from the file and
-- remove this file after.
-- Returns an array of traces recorded during the run.
M.finish = function()
  assert(JDUMP_FILE ~= nil, 'jit_parse is not running')
  jdump.off()
  -- Enable traces compilation for `jit.dump` back.
  jit.on(jdump.on, true)
  jit.on(jdump.off, true)
  local traces, aborted_traces = parse_jit_dump()
  os.remove(JDUMP_FILE)
  JDUMP_FILE = nil
  return traces, aborted_traces
end

-- Turn off compilation for the module to avoid side effects.
jit.off(true, true)

return M
