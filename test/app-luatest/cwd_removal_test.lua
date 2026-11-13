local popen = require('popen')
local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.after_each(function()
    if g.ph ~= nil then
        g.ph:close()
        g.ph = nil
    end
end)

-- Tarantool must start in a removed current directory instead of
-- hanging.
g.test_start_in_removed_cwd = function()
    local dir = treegen.prepare_directory({})
    local cmd = ('cd %q && rmdir %q && %q -e "os.exit()"'):format(
        dir, dir, arg[-1])
    g.ph = popen.shell(cmd)
    local status, err = g.ph:wait({timeout = 60})
    t.assert_equals(status, {state = popen.state.EXITED, exit_code = 0}, err)
end

-- Modules must load after the current directory of a running
-- instance is removed, and a missing one is reported as usual.
g.test_require_after_cwd_removal = function()
    local dir = treegen.prepare_directory({})
    treegen.write_file(dir, 'mymod.lua', "return 'mymod'")
    treegen.write_file(dir, 'main.lua', string.dump(function()
        local fio = require('fio')
        local json = require('json')

        local cwd = fio.tempdir()
        assert(fio.chdir(cwd))
        assert(fio.rmdir(cwd))

        local _, err = pcall(require, 'nosuchmod')
        print(json.encode({
            mymod = require('mymod'),
            nosuchmod = err:match('^[^\n]*'),
        }))
    end))

    -- Leave the search root unset, so it is the current directory.
    local res = justrun.tarantool(dir, {}, {'main.lua'},
                                  {setsearchroot = false})
    t.assert_equals(res, {
        exit_code = 0,
        stdout = {{
            mymod = 'mymod',
            nosuchmod = "module 'nosuchmod' not found:",
        }},
    })
end
