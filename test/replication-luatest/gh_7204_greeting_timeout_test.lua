local fiber = require('fiber')
local server = require('luatest.server')
local socket = require('socket')
local t = require('luatest')

local g = t.group()

g.before_all(function(g)
    g.server = server:new({
        alias = 'master',
        box_cfg = {
            replication_timeout = 0.1,
            replication_connect_timeout = 0.5,
        },
    })
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.test_greeting_timeout = function(g)
    local uri = server.build_listen_uri('server', g.server.id)
    local s = socket.tcp_server('unix/', uri, {
        handler = function() fiber.sleep(9000) end
    })
    t.assert(s)
    g.server:exec(function(uri)
        box.cfg{replication = uri}
    end, {uri})
    t.helpers.retrying({}, function()
        t.assert(g.server:grep_log('timed out'))
        t.assert(g.server:grep_log('will retry'))
    end)
    s:close()
end
