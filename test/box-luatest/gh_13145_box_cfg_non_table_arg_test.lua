local t = require('luatest')
local server = require('luatest.server')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.test_initial_cfg_non_table_arg = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        local t = require('luatest')
        for _, bad in ipairs({42, 'x', false}) do
            t.assert_error_msg_contains('cfg should be a table', box.cfg, bad)
        end
        os.exit(0)
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, {'main.lua'}, opts)
    t.assert_equals(res.exit_code, 0, {res.stdout, res.stderr})
end

g.test_reload_cfg_non_table_arg = function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local t = require('luatest')
        for _, bad in ipairs({42, 'x', false}) do
            t.assert_error_msg_contains('cfg should be a table', box.cfg, bad)
        end
    end)
    cg.server:drop()
end

g.test_log_cfg_non_table_arg = function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local t = require('luatest')
        local log = require('log')
        for _, bad in ipairs({42, 'x', false}) do
            t.assert_error_msg_contains('cfg should be a table', log.cfg, bad)
        end
    end)
    cg.server:drop()
end
