local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
    }
    cg.server:start()
    cg.server:exec(function()
        local msgpack = require('msgpack')
        local fiber = require('fiber')

        box.session.on_connect(function()
            rawset(_G, 'sid', box.session.id())
        end)
        rawset(_G, 'test_iproto_send',
               function(mp_header, mp_body, lua_header, lua_body)
                   local packet_size = 5 + #mp_header +
                           (mp_body ~= nil and #mp_body or 0)
                   local mp = _G.s:read(packet_size)
                   local mp_len = #mp
                   t.assert_equals(mp_len, packet_size)
                   local packet_len, next = msgpack.decode(mp)
                   t.assert_equals(mp_len - next + 1, packet_len)
                   local packet_header, next = msgpack.decode(mp, next)
                   local packet_body
                   if mp_body ~= nil then
                       packet_body, next = msgpack.decode(mp, next)
                   end
                   t.assert_equals(next, mp_len + 1)
                   t.assert_equals(packet_header, lua_header)
                   t.assert_equals(packet_body, lua_body)
               end)
        local ch = fiber.channel(1)
        rawset(_G, 'test_channel', ch)
        rawset(_G, 'test_on_disconnect', function()
            local _, err = pcall(function() box.iproto.send(_G.sid, {}) end)
            ch:put(err)
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function(net_box_uri)
        local uri = require('uri')
        local socket = require('socket')

        -- Connect to the server.
        local u = uri.parse(net_box_uri)
        local s = socket.tcp_connect(u.host, u.service)
        t.assert_is_not(s, nil)
        -- Skip the greeting.
        t.assert_equals(#s:read(128), 128)
        rawset(_G, 's', s)
    end, {cg.server.net_box_uri})
end)

g.before_test('test_box_iproto_send_errors', function(cg)
    cg.server:exec(function()
        require('log').info('adding callback')
        box.session.on_disconnect(_G.test_on_disconnect)
    end)
end)

g.after_test('test_box_iproto_send_errors', function(cg)
    cg.server:exec(function()
        require('log').info('removing callback')
        box.session.on_disconnect(nil, _G.test_on_disconnect)
    end)
end)

-- Checks that `box.iproto.send` errors are handled correctly.
g.test_box_iproto_send_errors = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        -- Checks that invalid argument counts are handled correctly.
        local usage_err_msg = "Usage: box.iproto.send(sid, header[, body])"
        t.assert_error_msg_content_equals(usage_err_msg,
                                          function() box.iproto.send() end)
        t.assert_error_msg_content_equals(usage_err_msg,
                                          function() box.iproto.send(0) end)
        -- Checks that invalid session identifiers are handled correctly.
        t.assert_error_msg_content_equals("expected uint64_t as 1 argument",
                                          function()
                                              box.iproto.send('str', {})
                                          end)
        t.assert_error_msg_content_equals("Session 777 does not exist",
                                          function()
                                              box.iproto.send(777, {})
                                          end)
        -- Checks that invalid session type is handled correctly.
        local bg
        fiber.create(function() bg = box.session.id(); fiber.yield() end)
        t.assert_error_msg_content_equals(
                "Session 'background' is not supported",
                function() box.iproto.send(bg, {}) end)
        -- Checks that invalid 2 argument type is handled correctly.
        t.assert_error_msg_content_equals(
                "expected table or string as 2 argument",
                function() box.iproto.send(0, 0) end)
        -- Checks that invalid 3 argument type is handled correctly.
        t.assert_error_msg_content_equals(
                "expected table or string as 3 argument",
                function() box.iproto.send(0, {}, 0) end)

        local sid = _G.sid
        local s = _G.s
        -- Checks that closed session is handled correctly.
        s:close()
        local _, err = pcall(function() box.iproto.send(sid, {}) end)
        if err ~= nil then
            t.assert_str_contains(tostring(err), 'Session is closed')
        else
            err = _G.test_channel:get()
            t.assert_str_contains(tostring(err), 'Session is closed')
        end
    end)
end

-- Checks that `box.iproto.send` works correctly with raw MsgPack arguments.
g.test_box_iproto_send_raw_msgpack = function(cg)
    cg.server:exec(function()
        local msgpack = require('msgpack')

        local sid = _G.sid
        local test_iproto_send = _G.test_iproto_send

        local lua_header = {a = 1}
        local lua_body = {b = 1}
        local mp_header = msgpack.encode(lua_header)
        local mp_body = msgpack.encode(lua_body)

        -- Checks that packet consisting only of MsgPack header is sent
        -- correctly.
        box.iproto.send(sid, mp_header)
        test_iproto_send(mp_header, nil, lua_header)

        -- Checks that packet consisting of MsgPack header and body is sent
        -- correctly.
        box.iproto.send(sid, mp_header, mp_body)
        test_iproto_send(mp_header, mp_body, lua_header, lua_body)
    end)
end

-- Checks that `box.iproto.send` works correctly with Lua table arguments.
g.test_box_iproto_send_lua_tables = function(cg)
    cg.server:exec(function()
        local msgpack = require('msgpack')

        local sid = _G.sid
        local test_iproto_send = _G.test_iproto_send

        local lua_header = {a = 1}
        local lua_body = {b = 1}
        local mp_header = msgpack.encode(lua_header)
        local mp_body = msgpack.encode(lua_body)

        -- Checks that packet consisting only of Lua header is sent correctly.
        box.iproto.send(sid, lua_header)
        test_iproto_send(mp_header, nil, lua_header)

        -- Checks that packet consisting of Lua header and body is sent
        -- correctly.
        box.iproto.send(sid, lua_header, lua_body)
        test_iproto_send(mp_header, mp_body, lua_header, lua_body)
    end)
end

-- Checks that `box.iproto.send` works correctly with mixed Raw MsgPack and Lua
-- table arguments.
g.test_box_iproto_send_mixed_args = function(cg)
    cg.server:exec(function()
        local msgpack = require('msgpack')

        local sid = _G.sid
        local test_iproto_send = _G.test_iproto_send

        local lua_header = {a = 1}
        local lua_body = {b = 1}
        local mp_header = msgpack.encode(lua_header)
        local mp_body = msgpack.encode(lua_body)

        -- Checks that packet consisting of MsgPack header and Lua body is sent
        -- correctly.
        box.iproto.send(sid, mp_header, lua_body)
        test_iproto_send(mp_header, mp_body, lua_header, lua_body)

        -- Checks that packet consisting of Lua header and MsgPack body is sent
        -- correctly.
        box.iproto.send(sid, lua_header, mp_body)
        test_iproto_send(mp_header, mp_body, lua_header, lua_body)
    end)
end

-- Checks that `box.iproto.send` works correctly with Lua table arguments
-- containing IPROTO key strings that need to be translated.
g.test_box_iproto_send_lua_tables_with_translation = function(cg)
    cg.server:exec(function()
        local msgpack = require('msgpack')

        local sid = _G.sid
        local test_iproto_send = _G.test_iproto_send

        local iproto_header = {sync = 7, REQUEST_TYPE = 8}
        local translated_iproto_header =
            setmetatable({[box.iproto.key.SYNC] = 7,
                          [box.iproto.key.REQUEST_TYPE] = 8},
                         { __serialize = 'map'})
        local iproto_body = {data = {1, 2, 3}, ERROR_24 = 'error'}
        local translated_iproto_body =
            setmetatable({[box.iproto.key.DATA] = {1, 2, 3},
                          [box.iproto.key.ERROR_24] = 'error'},
                         { __serialize = 'map'})
        local mp_header = msgpack.encode(translated_iproto_header)
        local mp_body = msgpack.encode(translated_iproto_body)

        -- Checks that packet consisting only of Lua header with IPROTO key is
        -- translated and sent correctly.
        box.iproto.send(sid, iproto_header)
        test_iproto_send(mp_header, nil, translated_iproto_header)

        -- Checks that packet consisting of Lua header and body with IPROTO keys
        -- is translated and sent correctly.
        box.iproto.send(sid, iproto_header, iproto_body)
        test_iproto_send(mp_header, mp_body,
                         translated_iproto_header, translated_iproto_body)
    end)
end
