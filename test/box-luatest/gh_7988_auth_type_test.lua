local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')
local urilib = require('uri')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.user.create('test', {password = 'secret'})
        box.session.su('admin', box.schema.user.grant, 'test', 'super')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_box_cfg = function(cg)
    cg.server:exec(function()
        local t = require('luatest')
        t.assert_equals(box.cfg.auth_type, 'chap-sha1')
        t.assert_error_msg_equals(
            "Incorrect value for option 'auth_type': should be of type string",
            box.cfg, {auth_type = 42})
        t.assert_error_msg_equals(
            "Incorrect value for option 'auth_type': chap-sha128",
            box.cfg, {auth_type = 'chap-sha128'})
    end)
end

g.test_net_box = function(cg)
    local parsed_uri = urilib.parse(cg.server.net_box_uri)
    parsed_uri.login = 'test'
    parsed_uri.password = 'secret'
    parsed_uri.params = parsed_uri.params or {}
    parsed_uri.params.auth_type = {'chap-sha128'}
    local uri = urilib.format(parsed_uri, true)
    t.assert_error_msg_equals(
        "Unknown authentication method 'chap-sha128'",
        net.connect, uri)
    parsed_uri.params.auth_type = {'chap-sha1'}
    uri = urilib.format(parsed_uri, true)
    local conn = net.connect(cg.server.net_box_uri, uri)
    t.assert_equals(conn.error, nil)
    conn:close()
    t.assert_error_msg_equals(
        "Unknown authentication method 'chap-sha128'",
        net.connect, uri, {auth_type = 'chap-sha128'})
    t.assert_error_msg_equals(
        "Unknown authentication method 'chap-sha128'",
        net.connect, cg.server.net_box_uri, {
            user = 'test',
            password = 'secret',
            auth_type = 'chap-sha128',
        })
    conn = net.connect(cg.server.net_box_uri, {
        user = 'test',
        password = 'secret',
        auth_type = 'chap-sha1',
    })
    t.assert_equals(conn.error, nil)
    conn:close()
end

g.before_test('test_replication', function(cg)
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            replication = server.build_listen_uri('master'),
        },
    })
    cg.replica:start()
end)

g.after_test('test_replication', function(cg)
    cg.replica:drop()
end)

g.test_replication = function(cg)
    cg.replica:exec(function(uri)
        local t = require('luatest')
        local urilib = require('uri')
        local parsed_uri = urilib.parse(uri)
        parsed_uri.login = 'test'
        parsed_uri.password = 'secret'
        parsed_uri.params = parsed_uri.params or {}
        parsed_uri.params.auth_type = {'chap-sha128'}
        uri = urilib.format(parsed_uri, true)
        box.cfg({replication = {}})
        t.assert_error_msg_matches(
            "Incorrect value for option 'replication': " ..
            "bad URI '.*%?auth_type=chap%-sha128': " ..
            "unknown authentication method",
            box.cfg, {replication = uri})
        parsed_uri.params.auth_type = {'chap-sha1'}
        uri = urilib.format(parsed_uri, true)
        box.cfg({replication = uri})
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
    end, {server.build_listen_uri('master')})
end
