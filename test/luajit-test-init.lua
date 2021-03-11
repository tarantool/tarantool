-- Disable strict for Tarantool.
require("strict").off()

-- Add `strict.off()` to `progname` command, that runs child tests
-- in some LuaJIT test suites to disable strict there too.
-- Quotes type is important.
-- XXX: luacheck thinks that `arg` is read-only global variable.
-- luacheck: no global
arg[-1] = arg[-1]..' -e "require[[strict]].off()"'
