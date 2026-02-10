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

-- Test the IPROTO interface is not implemented.
g.test_iproto = function(cg)
    cg.server:exec(function(net_box_uri)
        local uri = require('uri')
        local socket = require('socket')

        -- Connect to the server.
        local u = uri.parse(net_box_uri)
        local s = socket.tcp_connect(u.host, u.service)
        local greeting = s:read(box.iproto.GREETING_SIZE)
        greeting = box.iproto.decode_greeting(greeting)
        t.assert_covers(greeting, {protocol = 'Binary'})

        -- Send the request.
        local request = box.iproto.encode_packet(
            {request_type = box.iproto.type.DELETE_RANGE, sync = 123})
        t.assert_equals(s:write(request), #request)

        -- Read the response.
        local response = ''
        local header, body
        repeat
            header, body = box.iproto.decode_packet(response)
            if header == nil then
                local size = body
                local data = s:read(size)
                t.assert_is_not(data)
                response = response .. data
            end
        until header ~= nil
        s:close()

        t.assert_equals(body[box.iproto.key.ERROR_24],
                        "Unknown request type 18")
    end, {cg.server.net_box_uri})
end
