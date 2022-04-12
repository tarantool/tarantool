local net = require('net.box')
local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all = function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.schema.user.create('alice')
        box.schema.user.passwd('alice', 'ALICE')
        box.session.su('admin', box.schema.user.grant,
                       'alice', 'execute', 'universe')
        box.session.on_disconnect(function()
            rawset(_G, 'peer', box.session.peer())
        end)
    end)
end

g.after_all = function()
    g.server:drop()
end

g.test_session_disconnect_peer = function()
    local c = net.connect(g.server.net_box_uri,
                          {user = 'alice', password = 'ALICE'})
    local peer = c:call('box.session.peer')
    t.assert_is_not(peer, nil)
    c:close()
    g.server:exec(function(expected_peer)
        local t = require('luatest')
        t.helpers.retrying({}, function()
            local peer = rawget(_G, 'peer')
            t.assert_equals(peer, expected_peer)
        end)
    end, {peer})
end
