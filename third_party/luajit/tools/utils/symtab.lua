-- Parser of LuaJIT's symtab binary stream.
-- The format spec can be found in <src/lj_memprof.h>.
--
-- Major portions taken verbatim or adapted from the LuaVela.
-- Copyright (C) 2015-2019 IPONWEB Ltd.

local bit = require "bit"

local avl = require "utils.avl"

local band = bit.band
local string_format = string.format

local LJS_MAGIC = "ljs"
local LJS_CURRENT_VERSION = 0x3
local LJS_EPILOGUE_HEADER = 0x80
local LJS_SYMTYPE_MASK = 0x03

local SYMTAB_LFUNC = 0
local SYMTAB_CFUNC = 1
local SYMTAB_TRACE = 2

local M = {}

function M.loc(args)
  local loc = {
    addr = args.addr or 0,
    line = args.line or 0,
    traceno = args.traceno or 0,
  }

  return loc
end

-- Parse a single entry in a symtab: lfunc symbol.
function M.parse_sym_lfunc(reader, symtab)
  local sym_addr = reader:read_uleb128()
  local sym_chunk = reader:read_string()
  local sym_line = reader:read_uleb128()

  symtab.lfunc[sym_addr] = symtab.lfunc[sym_addr] or {}

  if sym_chunk:find('\n') and not symtab.alias[sym_chunk] then
    table.insert(symtab.alias, sym_chunk)
    symtab.alias[sym_chunk] = string_format(
      "function_alias_%d", #symtab.alias
    )
  end

  symtab.lfunc[sym_addr] = {
    source = sym_chunk,
    linedefined = sym_line,
  }
end

function M.parse_sym_trace(reader, symtab)
  local traceno = reader:read_uleb128()
  local sym_addr = reader:read_uleb128()
  local sym_line = reader:read_uleb128()

  symtab.trace[traceno] = symtab.trace[traceno] or {}

  symtab.trace[traceno] = {
    start = M.loc({ addr = sym_addr, line = sym_line })
  }
end

-- Parse a single entry in a symtab: .so library
function M.parse_sym_cfunc(reader, symtab)
  local addr = reader:read_uleb128()
  local name = reader:read_string()

  symtab.cfunc = avl.insert(symtab.cfunc, addr, {
    name = name
  })
end

local parsers = {
  [SYMTAB_LFUNC] = M.parse_sym_lfunc,
  [SYMTAB_TRACE] = M.parse_sym_trace,
  [SYMTAB_CFUNC] = M.parse_sym_cfunc,
}

function M.parse(reader)
  local symtab = {
    lfunc = {},
    trace = {},
    alias = {},
    cfunc = nil,
  }
  local magic = reader:read_octets(3)
  local version = reader:read_octets(1)

  -- Dummy-consume reserved bytes.
  local _ = reader:read_octets(3)

  if magic ~= LJS_MAGIC then
    error("Bad LuaJIT symbol table format prologue: " .. tostring(magic))
  end

  if string.byte(version) ~= LJS_CURRENT_VERSION then
    error(string_format(
         "LuaJIT symbol table format version mismatch: "..
         "the tool expects %d, but your data is %d",
         LJS_CURRENT_VERSION,
         string.byte(version)
    ))

  end

  while not reader:eof() do
    local header = reader:read_octet()
    local is_final = band(header, LJS_EPILOGUE_HEADER) ~= 0

    if is_final then
      break
    end

    local sym_type = band(header, LJS_SYMTYPE_MASK)
    if parsers[sym_type] then
      parsers[sym_type](reader, symtab)
    end
  end

  return symtab
end

function M.id(loc)
  return string_format(
    "f%#xl%dt%dg", loc.addr, loc.line, loc.traceno
  )
end

local function demangle_trace(symtab, loc)
  local traceno = loc.traceno

  assert(traceno ~= 0, "Location is a trace")

  local trace_str = string_format("TRACE [%d]", traceno)
  local trace = symtab.trace[traceno]

  if trace then
    assert(trace.start.traceno == 0, "Trace start is not a trace")
    return trace_str.." started at "..M.demangle(symtab, trace.start)
  end
  return trace_str
end

function M.demangle(symtab, loc)
  if loc.traceno ~= 0 and loc.traceno ~= nil then
    return demangle_trace(symtab, loc)
  end

  local addr = loc.addr

  if addr == 0 then
    return "INTERNAL"
  end

  if symtab.lfunc[addr] then
    local source = symtab.lfunc[addr].source
    return string_format("%s:%d", symtab.alias[source] or source, loc.line)
  end

  local key, value = avl.floor(symtab.cfunc, addr)

  if key then
    return string_format("%s:%#x", value.name, key)
  end

  return string_format("CFUNC %#x", addr)
end

return M
