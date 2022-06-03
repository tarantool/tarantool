local misc = require('test.luatest_helpers.misc')
local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.test_walext_unavailable = function()
    misc.skip_if_enterprise()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        local t = require('luatest')
        t.assert_error_msg_equals(
            "Community edition does not support WAL extensions",
            box.cfg, { wal_ext = {} })
    end)
end

g.after_test('test_walext_unavailable', function()
    g.server:drop()
    g.server = nil
end)
