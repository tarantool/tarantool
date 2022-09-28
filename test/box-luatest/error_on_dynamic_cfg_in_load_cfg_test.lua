local server = require('test.luatest_helpers.server')
local misc = require('test.luatest_helpers.misc')
local t = require('luatest')
local fio = require('fio')
local g = t.group()

g.before_test("test_error_on_dynamic_cfg_in_load_cfg", function()
    g.server = server:new{alias = 'master', box_cfg = {wal_ext = {}}}
end)

g.after_test("test_error_on_dynamic_cfg_in_load_cfg", function()
    g.server:cleanup()
end)

g.test_error_on_dynamic_cfg_in_load_cfg = function()
    misc.skip_if_enterprise()
    g.server:start{wait_for_readiness = false}
    t.helpers.retrying({}, function()
        local msg = "Community edition does not support WAL extensions"
        local filename = fio.pathjoin(g.server.workdir, g.server.alias..'.log')
        t.assert(g.server:grep_log(msg, nil, {filename = filename}))
    end)
end
