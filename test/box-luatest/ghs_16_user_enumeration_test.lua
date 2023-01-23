local msgpack = require('msgpack')
local net = require('net.box')
local server = require('luatest.server')
local socket = require('socket')
local urilib = require('uri')
local t = require('luatest')

local g = t.group()

local IPROTO_REQUEST_TYPE = 0
local IPROTO_TYPE_ERROR = bit.lshift(1, 15)
local IPROTO_AUTH = 7
local IPROTO_TUPLE = 33
local IPROTO_USER = 35
local IPROTO_ERROR = 49

-- Opens a new connection and sends IPROTO_AUTH request.
-- Returns {code, error}
local function auth(sock_path, user, tuple)
    local hdr = msgpack.encode({[IPROTO_REQUEST_TYPE] = IPROTO_AUTH})
    local body = msgpack.encode({
        [IPROTO_USER] = user,
        [IPROTO_TUPLE] = tuple,
    })
    local len = hdr:len() + body:len()
    t.assert_lt(len, 256)
    local s = socket.tcp_connect('unix/', sock_path)
    local data = s:read(128) -- greeting
    t.assert_equals(#data, 128)
    data = '\xce\00\00\00' .. string.char(len) .. hdr .. body
    t.assert_equals(s:write(data), #data) -- request
    data = s:read(5) -- fixheader
    t.assert_equals(#data, 5)
    len = msgpack.decode(data)
    data = s:read(len) -- response
    t.assert_equals(#data, len)
    s:close()
    hdr, len = msgpack.decode(data)
    body = msgpack.decode(data, len)
    return {
        bit.band(hdr[IPROTO_REQUEST_TYPE], bit.bnot(IPROTO_TYPE_ERROR)),
        body[IPROTO_ERROR],
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
