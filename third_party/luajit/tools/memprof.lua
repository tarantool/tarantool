-- A tool for parsing and visualisation of LuaJIT's memory
-- profiler output.
--
-- TODO:
-- * Think about callgraph memory profiling for complex
--   table reallocations
-- * Nicer output, probably an HTML view
-- * Demangling of C symbols
--
-- Major portions taken verbatim or adapted from the LuaVela.
-- Copyright (C) 2015-2019 IPONWEB Ltd.

local memprof = require "memprof.parse"
local process = require "memprof.process"
local evread = require "utils.evread"
local view = require "memprof.humanize"

local stdout, stderr = io.stdout, io.stderr
local match, gmatch = string.match, string.gmatch

-- Program options.
local opt_map = {}

-- Default config for the memprof parser.
local config = {
  leak_only = false,
  human_readable = false,
}

function opt_map.help()
  stdout:write [[
luajit-parse-memprof - parser of the memory usage profile collected
                       with LuaJIT's memprof.

SYNOPSIS

luajit-parse-memprof [options] memprof.bin

Supported options are:

  --help                            Show this help and exit
  --human-readable                  Use KiB/MiB/GiB notation instead of bytes
  --leak-only                       Report only leaks information
]]
  os.exit(0)
end

opt_map["leak-only"] = function()
  config.leak_only = true
end

opt_map["human-readable"] = function()
  config.human_readable = true
end

-- Print error and exit with error status.
local function opterror(...)
  stderr:write("luajit-parse-memprof.lua: ERROR: ", ...)
  stderr:write("\n")
  os.exit(1)
end

-- Parse single option.
local function parseopt(opt, args)
  local opt_current = #opt == 1 and "-"..opt or "--"..opt
  local f = opt_map[opt]
  if not f then
    opterror("unrecognized option `", opt_current, "'. Try `--help'.\n")
  end
  f(args)
end

-- Parse arguments.
local function parseargs(args)
  -- Process all option arguments.
  args.argn = 1
  repeat
    local a = args[args.argn]
    if not a then
      break
    end
    local lopt, opt = match(a, "^%-(%-?)(.+)")
    if not opt then
      break
    end
    args.argn = args.argn + 1
    if lopt == "" then
      -- Loop through short options.
      for o in gmatch(opt, ".") do
        parseopt(o, args)
      end
    else
      -- Long option.
      parseopt(opt, args)
    end
  until false

  -- Check for proper number of arguments.
  local nargs = #args - args.argn + 1
  if nargs ~= 1 then
    opt_map.help()
  end

  -- Translate a single input file.
  -- TODO: Handle multiple files?
  return args[args.argn]
end

local function dump(inputfile)
  -- XXX: This function exits with a non-zero exit code and
  -- prints an error message if it encounters any failure during
  -- the process of parsing.
  local events, symbols = evread(memprof.parse, inputfile)

  if not config.leak_only then
    view.profile_info(events, config)
  end
  local dheap = process.form_heap_delta(events)
  view.leak_info(dheap, config)
  view.aliases(symbols)
  -- XXX: The second argument is required to properly close Lua
  -- universe (i.e. invoke <lua_close> before exiting).
  os.exit(0, true)
end

-- XXX: When this script is used as a preloaded module by an
-- application, it should return one function for correct parsing
-- of command line flags like --leak-only and dumping profile
-- info.
local function dump_wrapped(...)
  return dump(parseargs(...))
end

return dump_wrapped
