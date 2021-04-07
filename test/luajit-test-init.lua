-- Disable strict for Tarantool.
require("strict").off()

-- XXX: lua-Harness test suite uses it's own tap.lua module
-- that conflicts with the Tarantool's one.
package.loaded.tap = nil
-- XXX: lua-Harness test suite checks that utf8 module presents
-- only in Lua5.3 or moonjit.
utf8 = nil

-- There are some tests launching Lua interpreter, so strict need
-- to be disabled for the child tests too. Hence `strict.off()` is
-- added to `progname` command used in these tests.
-- Unlike LuaJIT, Tarantool doesn't store the given CLI flags in
-- `arg`, so the table has the following layout:
-- * arg[-1] -- the binary name
-- * arg[0]  -- the script name
-- * arg[N]  -- the script argument for all N in [1, #arg]
-- The former one can be used to adjust the command to be used in
-- child tests.
-- XXX: Quotes types are important.
-- XXX: luacheck thinks that `arg` is read-only global variable.
-- luacheck: no global
arg[-1] = arg[-1]..' -e "require[[strict]].off()"'

-- XXX: PUC Rio Lua 5.1 test suite checks that global variable
-- `_loadfile()` exists and uses it for code loading from test
-- files. If the variable is not defined then suite uses
-- `loadfile()` as default. Same for the `_dofile()`.

-- XXX: Some tests in PUC Rio Lua 5.1 test suite clean `arg`
-- variable, so evaluate this once and use later.
local path_to_sources = arg[0]:gsub("[^/]+$", "")

-- luacheck: no global
function _loadfile(filename)
  return loadfile(path_to_sources..filename)
end

-- luacheck: no global
function _dofile(filename)
  return dofile(path_to_sources..filename)
end
