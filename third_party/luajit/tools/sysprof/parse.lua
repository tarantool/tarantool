-- Parser of LuaJIT's sysprof binary stream.
-- The format spec can be found in <src/lj_sysprof.h>.

local symtab = require "utils.symtab"
local vmdef = require "jit.vmdef"

local string_format = string.format

local LJP_MAGIC = "ljp"
local LJP_CURRENT_VERSION = 2

local M = {}

local VMST = {
  INTERP = 0,
  LFUNC  = 1,
  FFUNC  = 2,
  CFUNC  = 3,
  GC     = 4,
  EXIT   = 5,
  RECORD = 6,
  OPT    = 7,
  ASM    = 8,
  TRACE  = 9,
  SYMTAB = 10,
}


local FRAME = {
  LFUNC  = 1,
  CFUNC  = 2,
  FFUNC  = 3,
  BOTTOM = 0x80
}


local STREAM_END = 0x80
local SYMTAB_LFUNC_EVENT = 10
local SYMTAB_CFUNC_EVENT = 11
local SYMTAB_TRACE_EVENT = 12

local function new_event()
  return {
    lua = {
      vmstate = 0,
      callchain = {},
      trace = {
        traceno = nil,
        addr = 0,
        line = 0,
      }
    },
    host = {
      callchain = {}
    }
  }
end

local function parse_lfunc(reader, symbols)
  local addr = reader:read_uleb128()
  local line = reader:read_uleb128()
  local loc = symtab.loc({ addr = addr, line = line })
  loc.type = FRAME.LFUNC
  return symtab.demangle(symbols, loc)
end

local function parse_ffunc(reader, _)
  local ffid = reader:read_uleb128()
  return vmdef.ffnames[ffid]
end

local function parse_cfunc(reader, symbols)
  local addr = reader:read_uleb128()
  local loc = symtab.loc({ addr = addr })
  loc.type = FRAME.CFUNC
  return symtab.demangle(symbols, loc)
end

local frame_parsers = {
  [FRAME.LFUNC] = parse_lfunc,
  [FRAME.FFUNC] = parse_ffunc,
  [FRAME.CFUNC] = parse_cfunc
}

local function parse_lua_callchain(reader, event, symbols)
  while true do
    local frame_header = reader:read_octet()
    if frame_header == FRAME.BOTTOM then
      break
    end
    local name = frame_parsers[frame_header](reader, symbols)
    table.insert(event.lua.callchain, 1, {name = name, type = frame_header})
  end
end

--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~--

local function parse_host_callchain(reader, event, symbols)
  local addr = reader:read_uleb128()

  while addr ~= 0 do
    local loc = symtab.loc({ addr = addr })
    table.insert(event.host.callchain, 1, symtab.demangle(symbols, loc))
    addr = reader:read_uleb128()
  end
end

--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~--

local function parse_trace_callchain(reader, event, symbols)
  local loc = {}
  loc.traceno = reader:read_uleb128()
  loc.addr = reader:read_uleb128()
  loc.line = reader:read_uleb128()

  local name_lua = symtab.demangle(symbols, {
    addr = loc.addr,
    traceno = loc.traceno,
  })
  event.lua.trace = loc
  event.lua.trace.name = name_lua
end

--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~--

local function parse_host_only(reader, event, symbols)
  parse_host_callchain(reader, event, symbols)
end

local function parse_lua_host(reader, event, symbols)
  parse_lua_callchain(reader, event, symbols)
  parse_host_callchain(reader, event, symbols)
end

local function parse_trace(reader, event, symbols)
  parse_trace_callchain(reader, event, symbols)
  -- parse_lua_callchain(reader, event)
end

local function parse_symtab(reader, symbols, vmstate)
  if vmstate == SYMTAB_LFUNC_EVENT then
    symtab.parse_sym_lfunc(reader, symbols)
  elseif vmstate == SYMTAB_CFUNC_EVENT then
    symtab.parse_sym_cfunc(reader, symbols)
  elseif vmstate == SYMTAB_TRACE_EVENT then
    symtab.parse_sym_trace(reader, symbols)
  else
    error("Unknown symtab event")
  end
end

local event_parsers = {
  [VMST.INTERP] = parse_host_only,
  [VMST.LFUNC]  = parse_lua_host,
  [VMST.FFUNC]  = parse_lua_host,
  [VMST.CFUNC]  = parse_lua_host,
  [VMST.GC]     = parse_host_only,
  [VMST.EXIT]   = parse_host_only,
  [VMST.RECORD] = parse_host_only,
  [VMST.OPT]    = parse_host_only,
  [VMST.ASM]    = parse_host_only,
  [VMST.TRACE]  = parse_trace,
}

local function insert_lua_callchain(chain, lua)
  local ins_cnt = 0
  local name_lua
  for _, fr in ipairs(lua.callchain) do
    ins_cnt = ins_cnt + 1
    if fr.type == FRAME.CFUNC then
      -- C function encountered, the next chunk
      -- of frames is located on the C stack.
      break
    end
    name_lua = fr.name

    if fr.type == FRAME.LFUNC
      and lua.trace.traceno ~= nil
      and lua.trace.addr == fr.addr
      and lua.trace.line == fr.line
    then
      name_lua = lua.trace.name
    end

    table.insert(chain, name_lua)
  end
  table.remove(lua.callchain, ins_cnt)
end

local function merge(event)
  local callchain = {}

  for _, name_host in ipairs(event.host.callchain) do
    table.insert(callchain, name_host)
    if string.match(name_host, '^lua_cpcall') ~= nil then
      -- Any C function is present on both the C and the Lua
      -- stacks. It is more convenient to get its info from the
      -- host stack, since it has information about child frames.
      table.remove(event.lua.callchain)
    end

    if string.match(name_host, '^lua_p?call') ~= nil then
      insert_lua_callchain(callchain, event.lua)
    end

  end
  return table.concat(callchain, ';') .. ';'
end

local function parse_event(reader, events, symbols)
  local event = new_event()

  local vmstate = reader:read_octet()
  if vmstate == STREAM_END then
    -- TODO: samples & overruns
    return false
  elseif SYMTAB_LFUNC_EVENT <= vmstate and vmstate <= SYMTAB_TRACE_EVENT then
    parse_symtab(reader, symbols, vmstate)
    return true
  end

  assert(0 <= vmstate and vmstate <= 9, "Vmstate "..vmstate.." is not valid")
  event.lua.vmstate = vmstate

  event_parsers[vmstate](reader, event, symbols)
  local callchain = merge(event)
  events[callchain] = (events[callchain] or 0) + 1
  return true
end

function M.parse(reader, symbols)
  local events = {}

  local magic = reader:read_octets(3)
  local version = reader:read_octets(1)
  -- Dummy-consume reserved bytes.
  local _ = reader:read_octets(3)

  if magic ~= LJP_MAGIC then
    error("Bad sysprof event format prologue: " .. tostring(magic))
  end

  if string.byte(version) ~= LJP_CURRENT_VERSION then
    error(string_format(
      "Sysprof event format version mismatch:"..
      " the tool expects %d, but your data is %d",
      LJP_CURRENT_VERSION,
      string.byte(version)
    ))
  end

  while parse_event(reader, events, symbols) do
    -- Empty body.
  end

  return events
end

return M
