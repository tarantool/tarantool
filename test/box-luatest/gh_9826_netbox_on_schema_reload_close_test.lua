local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Tests that closing the connection from the `on_schema_reload` trigger works
-- correctly.
g.test_on_schema_reload_close = function(cg)
    local net_box = require('net.box')

    local c = net_box.connect(cg.server.net_box_uri, {wait_connected = false})
    c:on_schema_reload(function()
        pcall(function() c:close() end)
    end)
    t.assert(c:wait_state('closed', 0.1))
end
