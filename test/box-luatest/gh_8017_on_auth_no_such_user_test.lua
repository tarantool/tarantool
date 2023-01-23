local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.user.create('bob', {password = 'secret'})
        rawset(_G, 'last_auth', {})
        box.session.on_auth(function(user, status)
            _G.last_auth = {user, status}
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_on_auth_success = function(cg)
    local c = net.connect(cg.server.net_box_uri,
                          {user = 'bob', password = 'secret'})
    t.assert_equals(c.error, nil)
    c:close()
    cg.server:exec(function()
        t.assert_equals(_G.last_auth, {'bob', true})
        _G.last_auth = {}
    end)
end

g.test_on_auth_invalid_password = function(cg)
    local c = net.connect(cg.server.net_box_uri,
                          {user = 'bob', password = 'foobar'})
    t.assert_equals(c.error,
                    'User not found or supplied credentials are invalid')
    c:close()
    cg.server:exec(function()
        t.assert_equals(_G.last_auth, {'bob', false})
        _G.last_auth = {}
    end)
end

g.test_on_auth_no_such_user = function(cg)
    local c = net.connect(cg.server.net_box_uri,
                          {user = 'eve', password = 'foobar'})
    t.assert_equals(c.error,
                    'User not found or supplied credentials are invalid')
    c:close()
    cg.server:exec(function()
        t.assert_equals(_G.last_auth, {'eve', false})
        _G.last_auth = {}
    end)
end
