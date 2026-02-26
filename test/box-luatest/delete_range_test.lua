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

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.memtx ~= nil then
            box.space.memtx:drop()
        end
        if box.space.vinyl ~= nil then
            box.space.vinyl:drop()
        end
    end)
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

-- Test the feature does not work in CE engines.
g.test_lua = function(cg)
    cg.server:exec(function()
        local space_memtx = box.schema.create_space('memtx', {engine = 'memtx'})
        local space_vinyl = box.schema.create_space('vinyl', {engine = 'vinyl'})

        space_memtx:create_index('pk')
        space_vinyl:create_index('pk')

        t.assert_error_msg_equals('memtx does not support range deletion',
                                  space_memtx.delete_range, space_memtx, {}, {})
        t.assert_error_msg_equals('memtx does not support range deletion',
                                  space_memtx.index.pk.delete_range,
                                  space_memtx.index.pk, {}, {})
        t.assert_error_msg_equals('vinyl does not support range deletion',
                                  space_vinyl.delete_range, space_vinyl, {}, {})
        t.assert_error_msg_equals('vinyl does not support range deletion',
                                  space_vinyl.index.pk.delete_range,
                                  space_vinyl.index.pk, {}, {})
    end)
end
