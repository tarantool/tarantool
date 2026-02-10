local server = require('luatest.server')
local t = require('luatest')

local g = t.group('delete_range')

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        if box.space.vinyl ~= nil then
            box.space.vinyl:drop()
        end
        box.schema.user.drop('test', {if_exists = true})
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

-- Test the engines where the feature is not supported.
g.test_unsupported_engines = function(cg)
    cg.server:exec(function()
        local space_memtx = box.schema.create_space('test', {engine = 'memtx'})
        local space_vinyl = box.schema.create_space('vinyl', {engine = 'vinyl'})

        space_memtx:create_index('pk')
        space_vinyl:create_index('pk')

        t.assert_error_msg_equals('memtx does not support range delete',
                                  space_memtx.delete_range, space_memtx, {}, {})
        t.assert_error_msg_equals('memtx does not support range delete',
                                  space_memtx.index.pk.delete_range,
                                  space_memtx.index.pk, {}, {})
        t.assert_error_msg_equals('vinyl does not support range delete',
                                  space_vinyl.delete_range, space_vinyl, {}, {})
        t.assert_error_msg_equals('vinyl does not support range delete',
                                  space_vinyl.index.pk.delete_range,
                                  space_vinyl.index.pk, {}, {})
    end)
end

-- Test the API misusage.
g.test_schema_checks = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')

        -- No index.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_INDEX_ID',
        }, s.delete_range, s, 0.5)

        local i = s:create_index('pk')

        -- Wrong parameters.
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'Use space:delete_range(...)' ..
                      ' instead of space.delete_range(...)',
        }, s.delete_range)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'Use index:delete_range(...)' ..
                      ' instead of index.delete_range(...)',
        }, i.delete_range)

        -- User access denied.
        box.schema.user.create('test')
        t.assert_error_covers({
            type = 'AccessDeniedError',
            user = 'test',
            object_type = 'space',
            object_name = 'test',
        }, box.session.su, 'test', i.delete_range, i, {}, {})
    end)
end

-- Test the key range validation.
g.test_range_check = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local i = s:create_index('pk', {parts = {{1, 'unsigned'},
                                                 {2, 'unsigned'}}})

        -- Not passable key.
        local key = function() end
        local err = {
            type = 'LuajitError',
            message = "unsupported Lua type 'function'",
        }
        t.assert_error_covers(err, i.delete_range, i, key)
        t.assert_error_covers(err, i.delete_range, i, nil, key)
    end)
end
