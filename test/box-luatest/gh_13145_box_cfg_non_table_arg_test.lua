local t = require('luatest')
local server = require('luatest.server')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g.test_initial_cfg_non_table_arg = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        for _, bad in ipairs({42, 'x', false}) do
            local ok, err = pcall(box.cfg, bad)
            assert(not ok)
            assert(tostring(err):find('cfg should be a table', 1, true),
                   tostring(err))
        end
        print('ok')
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {}, {'main.lua'},
                                  {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout, 'ok')
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
end
