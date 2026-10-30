local net_box = require('net.box')
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

-- Tests that closing the connection from the `on_connect` trigger works
-- correctly.
g.test_on_connect_close = function(cg)
    local c = net_box.connect(cg.server.net_box_uri, {wait_connected = false})
    c:on_connect(function()
        pcall(function() c:close() end)
    end)
    t.assert(c:wait_state('closed', 10))
end
