local evread = require "utils.evread"
local sysprof = require "sysprof.parse"

local stdout, stderr = io.stdout, io.stderr
local match, gmatch = string.match, string.gmatch

-- Program options.
local opt_map = {}

function opt_map.help()
  stdout:write [[
luajit-parse-sysprof - parser of the profile collected
                       with LuaJIT's sysprof.

SYNOPSIS

luajit-parse-sysprof [options] sysprof.bin

Supported options are:

  --help                            Show this help and exit
]]
  os.exit(0)
end

-- Print error and exit with error status.
local function opterror(...)
  stderr:write("luajit-parse-sysprof.lua: ERROR: ", ...)
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
  local events = evread(sysprof.parse, inputfile)

  for stack, count in pairs(events) do
    print(stack, count)
  end
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
