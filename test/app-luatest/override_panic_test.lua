local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local g = t.group()

g.before_all(function(g)
    treegen.init(g)
end)

g.after_all(function(g)
    treegen.clean(g)
end)

-- Trigger luaT_newmodule() panic.
--
-- fio is required internally before box initialization, so we can
-- register fake box module from its override module. Then, when
-- box starts to register itself, luaT_newmodule() finds it as
-- already registered.
--
-- luaT_newmodule() must panic in the case.
g.test_newmodule_panic = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'override/fio.lua', [[
        local loaders = require('internal.loaders')
        loaders.builtin.box = {}
        return loaders.builtin.fio
    ]])
    treegen.write_script(dir, 'main.lua', '')
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, {'main.lua'}, opts)
    t.assert_equals(res, {
        exit_code = 1,
        stderr = 'luaT_newmodule(box): the module is already registered',
    })
end

-- Trigger luaT_setmodule() panic.
--
-- src/lua/fio.c registers `fio` module.
--
-- Then src/lua/fio.lua is loaded. It is structured like so:
--
--  | local fio = require('fio') -- from fio.c
--  |
--  | function fio.foo(<...>)
--  |     <...>
--  | end
--  |
--  | return fio
--
-- fio.lua's return value is passed to luaT_setmodule() and it
-- must be the same as one that is already registered by fio.c.
--
-- We can change what the require call returns (and so what
-- fio.lua returns) using an override module.
--
-- luaT_setmodule() must panic if it meets attempt to register
-- different values as the same built-in module.
g.test_setmodule_panic = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'override/fio.lua', [[
        return {}
    ]])
    treegen.write_script(dir, 'main.lua', '')
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, {'main.lua'}, opts)
    t.assert_equals(res, {
        exit_code = 1,
        stderr = 'luaT_setmodule(fio): the module is already registered ' ..
            'with another value',
    })
end
