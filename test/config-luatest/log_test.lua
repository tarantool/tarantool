local t = require('luatest')
local fio = require('fio')
local justrun = require('test.justrun')
local treegen = require('test.treegen')

local g = t.group()

g.before_all(function()
    treegen.init(g)
end)

g.after_all(function()
    treegen.clean(g)
end)

g.test_log_only_changed_options = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local script = [[
        box.cfg{log_level = 5, log = 'file:log.txt'}
        box.cfg{sql_cache_size = 1000}
        box.cfg{sql_cache_size = 1000}
        box.cfg{sql_cache_size = 1000}
        box.cfg{sql_cache_size = 2000}
        box.cfg{sql_cache_size = 2000}
        box.cfg{sql_cache_size = 1000}
        box.cfg{sql_cache_size = 1000}
        box.cfg{sql_cache_size = 1000}
        os.exit(0)
    ]]
    treegen.write_script(dir, 'main.lua', script)

    local env = {}
    local opts = {nojson = true, stderr = false}
    local args = {'main.lua'}
    local res = justrun.tarantool(dir, env, args, opts)
    t.assert_equals(res.exit_code, 0)

    local path = fio.pathjoin(dir, 'log.txt')
    local logs = {}
    for line in io.lines(path) do
        local res = string.gmatch(line, 'box.load_cfg I> set .+')()
        if res ~= nil then
            table.insert(logs, res)
        end
    end
    local exp = {
        [[box.load_cfg I> set 'log' configuration option to "file:log.txt"]],
        [[box.load_cfg I> set 'sql_cache_size' configuration option to 1000]],
        [[box.load_cfg I> set 'sql_cache_size' configuration option to 2000]],
        [[box.load_cfg I> set 'sql_cache_size' configuration option to 1000]],
    }
    t.assert_equals(logs, exp)
end
