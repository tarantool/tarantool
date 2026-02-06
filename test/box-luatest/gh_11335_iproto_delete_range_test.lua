local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function(net_box_uri)
        rawset(_G, 'iproto_delete_range', function(body_lua)
            local uri = require('uri')
            local socket = require('socket')
            -- Connect to the server.
            local u = uri.parse(net_box_uri)
            local s = socket.tcp_connect(u.host, u.service)
            local greeting = s:read(box.iproto.GREETING_SIZE)
            greeting = box.iproto.decode_greeting(greeting)
            t.assert_covers(greeting, {protocol = 'Binary'})
            -- Send the request.
            local mp = require('msgpack')
            local request = box.iproto.encode_packet(
                {request_type = box.iproto.type.DELETE_RANGE, sync = 123},
                mp.encode(setmetatable(body_lua, mp.map_mt))
            )
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
            return body
        end)
    end, {cg.server.net_box_uri})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

-- Test various invalid requests.
g.test_iproto_delete_range_invalid = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        -- No space ID.
        local r = _G.iproto_delete_range({})
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        "Missing mandatory field 'SPACE_ID' in request")

        -- No begin_key.
        r = _G.iproto_delete_range({
            [box.iproto.key.SPACE_ID] = 512,
        })
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        "Missing mandatory field 'KEY' in request")

        -- No end_key.
        r = _G.iproto_delete_range({
            [box.iproto.key.SPACE_ID] = 512,
            [box.iproto.key.KEY] = {},
        })
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        "Missing mandatory field 'TUPLE' in request")

        -- Invalid begin_key.
        r = _G.iproto_delete_range({
            [box.iproto.key.SPACE_ID] = 512,
            [box.iproto.key.KEY] = box.NULL,
            [box.iproto.key.TUPLE] = {},
        })
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        'Invalid MsgPack - packet body')

        -- Invalid end_key.
        r = _G.iproto_delete_range({
            [box.iproto.key.SPACE_ID] = 512,
            [box.iproto.key.KEY] = {},
            [box.iproto.key.TUPLE] = box.NULL,
        })
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        'Invalid MsgPack - packet body')

        -- A valid request, but memtx does not support range deletion.
        r = _G.iproto_delete_range({
            [box.iproto.key.SPACE_ID] = 512,
            [box.iproto.key.KEY] = {},
            [box.iproto.key.TUPLE] = {},
        })
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        'memtx does not support range deletion')

        -- The index ID included.
        r = _G.iproto_delete_range({
            [box.iproto.key.SPACE_ID] = 512,
            [box.iproto.key.INDEX_ID] = 0,
            [box.iproto.key.KEY] = {},
            [box.iproto.key.TUPLE] = {},
        })
        t.assert_equals(r[box.iproto.key.ERROR_24],
                        'memtx does not support range deletion')
    end)
end
