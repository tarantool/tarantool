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
            memtx_memory = '8GiB',
            vinyl_memory = '32 MiB',
        }
    })
    g.server:start()

    g.server:exec(function()
        t.assert_equals(box.cfg.memtx_memory, 8 * 1024 * 1024 * 1024)
        t.assert_equals(box.cfg.vinyl_memory, 32 * 1024 * 1024)
    end)
end
