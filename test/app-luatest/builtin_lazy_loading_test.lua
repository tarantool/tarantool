local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun').tarantool

local g = t.group()

g.before_all(function(cg)
    cg.dir = treegen.prepare_directory({}, {})
end)

-- A built-in module is loaded at its first require(), not at startup, and
-- its source is available all along.
g.test_loaded_at_first_require = function(cg)
    treegen.write_file(cg.dir, 'main.lua', [[
        local builtin = debug.getregistry()._TARANTOOL_BUILTIN
        local getsources = require('tarantool').debug.getsources
        local res = {
            before = rawget(builtin, 'csv') ~= nil,
            source = getsources('csv') ~= nil,
        }
        local csv = require('csv')
        res.after = rawget(builtin, 'csv') == csv
        res.works = csv.dump({{1, 2}}) == '1,2\n'
        print(require('json').encode(res))
    ]])
    local res = justrun(cg.dir, {}, {'main.lua'})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_equals(res.stdout, {{
        before = false,
        source = true,
        after = true,
        works = true,
    }})
end

-- The modules that set up globals and extend standard libraries are loaded
-- at startup all the same.
g.test_eager_modules = function(cg)
    treegen.write_file(cg.dir, 'eager.lua', [[
        local builtin = debug.getregistry()._TARANTOOL_BUILTIN
        print(require('json').encode({
            table_new = type(table.new),
            string_split = type(string.split),
            tarantool = rawget(builtin, 'tarantool') ~= nil,
        }))
    ]])
    local res = justrun(cg.dir, {}, {'eager.lua'})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_equals(res.stdout, {{
        table_new = 'function',
        string_split = 'function',
        tarantool = true,
    }})
end

-- Assigning to loaders.builtin replaces or removes a module that is not
-- loaded yet, as it would a loaded one: a script can shadow a built-in
-- module with its own this way.
g.test_shadow_builtin = function(cg)
    treegen.write_file(cg.dir, 'csv.lua', "return {whoami = 'mine'}")
    treegen.write_file(cg.dir, 'shadow.lua', [[
        package.loaded.csv = nil
        require('internal.loaders').builtin.csv = nil
        print(require('json').encode({whoami = require('csv').whoami}))
    ]])
    local res = justrun(cg.dir, {}, {'shadow.lua'})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_equals(res.stdout, {{whoami = 'mine'}})
end
