local msgpack = require('msgpack')
local server = require('luatest.server')
local socket = require('socket')
local t = require('luatest')
local uri = require('uri')

local g = t.group()

g.after_each(function(cg)
    if cg.socket ~= nil then
        cg.socket:close()
        cg.socket = nil
    end
    if cg.server ~= nil then
        cg.server:drop()
        cg.server = nil
    end
end)

g.test_on_connect_error = function(cg)
    cg.server = server:new()
    cg.server:start()

    -- An explicit error keeps the message independent of strict mode and
    -- the Lua source location.
    local message = 'on_connect trigger failed'
    cg.server:exec(function(message)
        local trigger = function() error(message, 0) end
        t.assert_type(box.session.on_connect(trigger), 'function')
    end, {message})

    local u = uri.parse(cg.server.net_box_uri)
    local timeout = 5
    local s, err = socket.tcp_connect(u.host, u.service, timeout)
    cg.socket = s
    t.assert(s, err)

    -- A new client gets the error after the greeting without sending a request.
    local iproto = box.iproto
    local greeting_size = 128
    local greeting = s:read(greeting_size, timeout)
    t.assert_equals(greeting and #greeting, greeting_size)

    -- The length prefix counts the header and body, not itself:
    --
    -- | packet size | header | body   |
    -- | MP_UINT32   | MP_MAP | MP_MAP |
    local prefix = s:read(5, timeout)
    t.assert_equals(prefix and #prefix, 5)
    local size, pos = msgpack.decode(prefix)
    t.assert_equals(pos, #prefix + 1)
    local payload = s:read(size, timeout)
    t.assert_equals(payload and #payload, size)
    local header, body_pos = msgpack.decode(payload)
    local body, end_pos = msgpack.decode(payload, body_pos)
    t.assert_equals(end_pos, #payload + 1)

    -- Bit 15 marks an error; the low 15 bits hold the error code.
    local error_type = bit.bor(iproto.type.TYPE_ERROR, box.error.PROC_LUA)
    t.assert_equals(header[iproto.key.REQUEST_TYPE], error_type)
    t.assert_equals(body[iproto.key.ERROR_24], message)

    -- An empty read confirms EOF; a timeout returns nil.
    t.assert_equals(s:read(1, timeout), '')
end
