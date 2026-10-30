local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

-- Checks that close() doesn't hang if called from on_connect trigger
-- of a connection that has watchers.
g.test_close_on_connect_after_watch = function()
    local conn = net.connect(g.server.net_box_uri, {
        user = 'test',
        password = 'test',
        wait_connected = false,
        reconnect_after = 0.01,
    })
    conn:watch('foo', function() end)
    conn:on_connect(function(conn)
        conn:close()
    end)
    g.server:exec(function()
        box.schema.user.create('test')
        box.schema.user.passwd('test', 'test')
    end)
    t.assert(conn:wait_state('closed', 10))
end
