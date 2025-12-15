-- XXX: PUC Rio Lua 5.1 test suite checks that global variable
-- `_loadfile()` exists and uses it for code loading from test
-- files. If the variable is not defined then suite uses
-- `loadfile()` as default. Same for the `_dofile()`.
-- lua-Harness also uses the same implementation of `dofile()`
-- for the same reason.

-- XXX: Some tests in PUC Rio Lua 5.1 test suite clean `arg`
-- variable, so evaluate this once and use later.
local path_to_sources = arg[0] and arg[0]:gsub("[^/]+$", "") or ""

-- luacheck: no global
function _loadfile(filename)
  return loadfile(path_to_sources..filename)
end

-- luacheck: no global
function _dofile(filename)
  return dofile(path_to_sources..filename)
end
