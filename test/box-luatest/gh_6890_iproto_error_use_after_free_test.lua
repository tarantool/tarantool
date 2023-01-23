local msgpack = require('msgpack')
local server = require('luatest.server')
local socket = require('socket')
local uri = require('uri')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.test_iproto_error_use_after_free = function()
    -- Connect to the server.
    local u = uri.parse(g.server.net_box_uri)
    local s = socket.tcp_connect(u.host, u.service)
    t.assert_is_not(s, nil)
    -- Skip the greeting.
    t.assert_equals(#s:read(128), 128)
    -- Send an invalid request and immediately close the socket so that
    -- the server fails to send the error back.
    local d = msgpack.encode('foo')
    t.assert_equals(s:write(d), #d)
    s:close()
    -- Check that the server logs the proper error.
    t.assert(g.server:grep_log('ER_INVALID_MSGPACK'))
end
