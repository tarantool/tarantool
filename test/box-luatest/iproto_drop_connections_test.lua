local net_box = require('net.box')
local t = require('luatest')
local server = require('luatest.server')
local it = require('test.interactive_tarantool')

local g = t.group()

g.after_each(function(g)
    if g.server ~= nil then
        g.server:stop()
    end
    if g.it ~= nil then
        g.it:close()
    end
    if g.conn ~= nil then
        g.conn:close()
    end
end)

-- Verify the internal function to drop iproto connections.
g.test_basic = function(g)
    -- Start a server.
    g.server = server:new({alias = 'server'})
    g.server:start()

    -- Start a console on the server to feed testing commands
    -- without iproto.
    g.server:exec(function()
        local console = require('console')

        console.listen('unix/:./tarantool.control')
    end)

    -- Connect to the server's console and verify that it works.
    g.it = it.connect(g.server)
    g.it:roundtrip('42', 42)

    -- Connect to the server using iproto and verify that it
    -- works.
    local uri = g.server.net_box_uri
    g.conn = net_box.connect(uri)
    t.assert(g.conn:ping())

    -- Drop connections.
    local timeout = 60
    g.it:roundtrip(('box.iproto.internal.drop_connections(%d)'):format(timeout))

    -- Verify that our connection was dropped.
    t.assert_not(g.conn:ping())

    -- However, we can reconnect.
    g.conn:close()
    g.conn = net_box.connect(uri)
    t.assert(g.conn:ping())

    -- Verify error reporting.
    t.assert_error_msg_equals('timed out', function()
        g.it:roundtrip('box.iproto.internal.drop_connections(0)')
    end)

    -- Verify that connections are dropped in background anyway
    -- despite the reported timeout error.
    t.helpers.retrying({timeout = 60}, function()
        t.assert_not(g.conn:ping())
    end)
end
