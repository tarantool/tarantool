local net = require('net.box')
local server = require('luatest.server')
local socket = require('socket')
local urilib = require('uri')
local t = require('luatest')

local g = t.group()

-- Opens a new connection and sends IPROTO_AUTH request.
-- Returns {code, error}
local function auth(sock_path, user, tuple)
    local s = socket.tcp_connect('unix/', sock_path)
    local greeting = s:read(box.iproto.GREETING_SIZE)
    greeting = box.iproto.decode_greeting(greeting)
    t.assert_covers(greeting, {protocol = 'Binary'})
    local request = box.iproto.encode_packet({
        sync = 123,
        request_type = box.iproto.type.AUTH,
    }, {
        user_name = user,
        tuple = tuple,
    })
    t.assert_equals(s:write(request), #request)
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
    t.assert_equals(header.sync, 123)
    return {
        bit.band(header.request_type, bit.bnot(box.iproto.type.TYPE_ERROR)),
        body.error_24,
    }
end

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.schema.user.create('test', {password = '1111'})
        box.schema.user.create('disabled')
        box.schema.user.revoke('disabled', 'session', 'universe')
    end)
end)

g.after_all(function()
    g.server:stop()
end)

-- If we raise different errors in case of entering an invalid password and
-- entering the login of a non-existent user during authorization, it will
-- open the door for an unauthorized person to enumerate users.
-- So raised errors must be the same in the cases described above.
g.test_user_enum_on_auth = function()
    local uri = urilib.parse(g.server.net_box_uri)
    local err_msg = 'User not found or supplied credentials are invalid'
    local cmd = 'return box.session.info()'
    local c = net.connect('test:1112@' .. uri.unix)
    t.assert_error_msg_contains(err_msg, c.eval , c, cmd)
    c:close()
    c = net.connect('nobody:1112@' .. uri.unix)
    t.assert_error_msg_contains(err_msg, c.eval , c, cmd)
    c:close()
end

-- Checks that it's impossible to figure out if a user exists by analyzing
-- the error sent in reply to a malformed authentication request.
-- https://github.com/tarantool/security/issues/21
g.test_user_enum_on_malformed_auth = function()
    local uri = g.server.net_box_uri
    for _, user in ipairs({'admin', 'test', 'disabled', 'no_such_user'}) do
        local msg = string.format("Invalid error for user '%s'", user)
        t.assert_equals(auth(uri, user, 42), {
            box.error.INVALID_MSGPACK,
            'Invalid MsgPack - packet body',
        }, msg)
        t.assert_equals(auth(uri, user, {}), {
            box.error.INVALID_MSGPACK,
            'Invalid MsgPack - authentication request body',
        }, msg)
        t.assert_equals(auth(uri, user, {42}), {
            box.error.INVALID_MSGPACK,
            'Invalid MsgPack - authentication request body',
        }, msg)
        t.assert_equals(auth(uri, user, {'foobar'}), {
            box.error.INVALID_MSGPACK,
            'Invalid MsgPack - authentication request body',
        }, msg)
        t.assert_equals(auth(uri, user, {'foobar', 'foobar'}), {
            box.error.UNKNOWN_AUTH_METHOD,
            "Unknown authentication method 'foobar'",
        }, msg)
        t.assert_equals(auth(uri, user, {'chap-sha1', 42}), {
            box.error.INVALID_AUTH_REQUEST,
            "Invalid 'chap-sha1' request: scramble must be string",
        }, msg)
        t.assert_equals(auth(uri, user, {'chap-sha1', 'foobar'}), {
            box.error.INVALID_AUTH_REQUEST,
            "Invalid 'chap-sha1' request: invalid scramble size",
        }, msg)
        t.assert_equals(auth(uri, user, {'chap-sha1', string.rep('x', 20)}), {
            box.error.CREDS_MISMATCH,
            'User not found or supplied credentials are invalid',
        }, msg)
    end
end
