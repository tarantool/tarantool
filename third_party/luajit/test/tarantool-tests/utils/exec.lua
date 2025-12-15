local M = {}

local function executable_idx(args)
  -- arg[-1] is guaranteed to be not nil.
  local idx = -2
  while args[idx] do
    assert(type(args[idx]) == 'string', 'Command part have to be a string')
    idx = idx - 1
  end
  return idx + 1
end

function M.luabin(args)
  -- Return only the executable.
  return args[executable_idx(args)]
end

function M.luacmd(args)
  -- Return the full command with flags.
  return table.concat(args, ' ', executable_idx(args), -1)
end

local function makeenv(tabenv)
  if tabenv == nil then return '' end
  local flatenv = {}
  for var, value in pairs(tabenv) do
    if var == 'LD_PRELOAD' then
      -- XXX: For ASan used by GCC, the ASan library should go
      -- first in the `LD_PRELOAD` list, or we get the error:
      -- "ASan runtime does not come first in initial library
      -- list;".
      -- See https://github.com/tarantool/tarantool/issues/9656
      -- for details.
      -- So, prepend the given library from `LD_PRELOAD` to the
      -- start of the list. The separator may be ':' or ' ', see
      -- man ld.so(8).so. Use ':' as the most robust.
      local ld_preload = os.getenv('LD_PRELOAD')
      local ld_preload_prefix = ld_preload and ld_preload .. ':' or ''
      value = ld_preload_prefix .. value
    end
    table.insert(flatenv, ('%s=%s'):format(var, value))
  end
  return table.concat(flatenv, ' ')
end

-- <makecmd> creates a command that runs %testname%/script.lua by
-- <LUAJIT_TEST_BINARY> with the given environment, launch options
-- and CLI arguments. The function yields an object (i.e. table)
-- with the aforementioned parameters. To launch the command just
-- call the object.
function M.makecmd(arg, opts)
  return setmetatable({
    LUABIN = M.luacmd(arg),
    SCRIPT = opts and opts.script or arg[0]:gsub('%.test%.lua$', '/script.lua'),
    ENV = opts and makeenv(opts.env) or '',
    REDIRECT = opts and opts.redirect or '',
  }, {
    __call = function(self, ...)
      -- This line just makes the command for <io.popen> by the
      -- following steps:
      -- 1. Replace the placeholders with the corresponding values
      --    given to the command constructor (e.g. script, env).
      -- 2. Join all CLI arguments given to the __call metamethod.
      -- 3. Concatenate the results of step 1 and step 2 to obtain
      --    the resulting command.
      local cmd = ('<ENV> <LUABIN> <REDIRECT> <SCRIPT>'):gsub('%<(%w+)>', self)
                  .. (' %s'):rep(select('#', ...)):format(...)
      -- Trim both leading and trailing whitespace from the output
      -- produced by the child process.
      return io.popen(cmd):read('*all'):gsub('^%s+', ''):gsub('%s+$', '')
    end
  })
end

return M
