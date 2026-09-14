local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_listen = function()
    g.server:exec(function()
        local listen = box.cfg.listen
        box.cfg({listen = {listen, params = {transport = 'plain'}}})
        t.assert_error_msg_equals(
            'Invalid transport: foo',
            box.cfg, {listen = {listen, params = {transport = 'foo'}}})
        box.cfg({listen = listen})
    end)
end

g.test_replication = function()
    g.server:exec(function()
        local listen = box.cfg.listen
        box.cfg({replication = {listen, params = {transport = 'plain'}}})
        t.assert_error_msg_equals(
            'Invalid transport: foo',
            box.cfg, {replication = {listen, params = {transport = 'foo'}}})
        box.cfg({replication = {}})
    end)
end

g.test_net_box = function()
    local c = net.connect({g.server.net_box_uri,
                           params = {transport = 'plain'}})
    t.assert_equals(c.state, 'active')
    c:close()
    t.assert_error_msg_equals(
        'Invalid transport: foo',
        net.connect, {g.server.net_box_uri, params = {transport = 'foo'}})
end
