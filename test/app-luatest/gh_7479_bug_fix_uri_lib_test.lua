local t = require('luatest')
local net_box = require('net.box')
local server = require('luatest.server')

local g = t.group()

-- Create test instance.
g.before_all(function()
    g.server = server:new({alias = 'test-gh-7479'})
    g.server:start()
    -- Get port for connection testing.
    g.server_port = g.server:exec(function()
        return require('console').listen(0):name()['port']
    end)
end)

-- Stop test instance.
g.after_all(function()
    g.server:stop()
end)

-- Checks whether it is possible to specify only port for net.box.connect.
g.test_net_box_connect_with_only_port_indication = function()
    -- Indicate only port, without localhost:* before the port value.
    local ok, _ = pcall(net_box.connect, nil, g.server_port)
    t.assert_equals(ok, true)
end
