-- Disable strict for Tarantool.
-- Required by: PUC-Rio-Lua-5.1-tests.
require("strict").off()

-- XXX: tarantool-tests suite uses it's own tap.lua module
-- that conflicts with the Tarantool's one.
-- Required by: tarantool-tests.
package.loaded.tap = nil
-- Use Tarantool specific profile for lua-Harness test suite.
-- Required by: lua-Harness.
pcall(require, 'profile_tarantool')

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
-- Required by: PUC-Rio-Lua-5.1-tests, lua-Harness-tests.
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

-- Required by: PUC-Rio-Lua-5.1-tests.
-- luacheck: no global
function _loadfile(filename)
  return loadfile(path_to_sources..filename)
end

-- Required by: PUC-Rio-Lua-5.1-tests, lua-Harness-tests.
-- luacheck: no global
function _dofile(filename)
  return dofile(path_to_sources..filename)
end

-- GC-related tests might fail due to the garbage generated on
-- Tarantool instance startup. Run full GC cycle to return it
-- to the initial state. For more info see the issue below:
-- https://github.com/tarantool/tarantool/issues/7058
-- Required by: tarantool-tests, PUC-Rio-Lua-5.1-tests,
--             LuaJIT-tests, lua-Harness-tests.
collectgarbage('collect')
