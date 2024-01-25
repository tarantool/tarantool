local t = require('luatest')
local treegen = require('test.treegen')
local fio = require('fio')

local g = t.group()

-- XXX: I would rather tweak <package.path>, since it's the
-- recommended way to manage the hints for the loaders. However,
-- the loader provided by LuaJIT that uses <package.path> works
-- fine (i.e. throws the expected error), unlike the ones added
-- for Tarantool. Hence, the easiest way to check Tarantool
-- loaders is switching the current working directory for the
-- test. Preserve the original working directory to restore it at
-- the end of the test.
local cwd = fio.cwd()

local name = 'syntax'
-- XXX: luatest uses only `string.find()` with 4th argument
-- (`plain`) unconditionally set to `true` for error matching, so
-- there is no way to use any pattern here.
local errtail = ("/%s.lua:1: unexpected symbol near '?'"):format(name)

g.before_all(function()
    treegen.init(g)
    treegen.add_template(g, ('^%s%%.lua$'):format(name), [[?syntax error?]])
    local luadir = treegen.prepare_directory(g, {('%s.lua'):format(name)})
    fio.chdir(luadir)
end)

g.after_all(function(g)
    treegen.clean(g)
    fio.chdir(cwd)
end)

g.test_loader_error_handling = function()
    t.assert_error_msg_contains(errtail, require, name)
end
