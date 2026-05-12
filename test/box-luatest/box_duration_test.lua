local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.after_all(function(g)
    if g.server ~= nil then
        g.server:drop()
        g.server = nil
    end
end)

g.test_box_cfg_human_readable_values = function(g)
    g.server = server:new({
        box_cfg = {
            replication_timeout = '2m',
            replication_connect_timeout = '30s',
            election_timeout = '300ms',
        }
    })
    g.server:start()

    g.server:exec(function()
        t.assert_equals(box.cfg.replication_timeout, 120)
        t.assert_equals(box.cfg.replication_connect_timeout, 30)
        t.assert_equals(box.cfg.election_timeout, 0.3)
    end)
end
