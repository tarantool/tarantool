local net = require('net.box')
local server = require('test.luatest_helpers.server')
local urilib = require('uri')
local t = require('luatest')
local g = t.group()

g.before_all = function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.schema.user.create('test', {password = '1111'})
    end)
end

g.after_all = function()
    g.server:stop()
end

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
