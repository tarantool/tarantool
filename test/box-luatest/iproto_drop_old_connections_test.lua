local t = require('luatest')
local uri = require('uri')
local server = require('luatest.server')
local net_box = require('net.box')

local g = t.group()

local function update_uri_param(uri_str, val)
    local uri_parsed = uri.parse(uri_str)
    uri_parsed.params = { test = val }
    return uri.format(uri_parsed)
end

g.before_each(function()
    local luatest_net_box_uri = server.build_listen_uri("master_luatest")
    g.mut_net_box_uri = update_uri_param(
        server.build_listen_uri("master_mut"), "before"
    )
    g.server = server:new({
        alias = 'master',
        box_cfg = {listen = {luatest_net_box_uri, g.mut_net_box_uri}},
        net_box_uri = luatest_net_box_uri
    })
    g.server:start()
end)

g.after_each(function()
    g.server:drop()
end)

g.test_reconnect_uri_param_change = function(g)
    local conn_old = net_box.connect(g.mut_net_box_uri)
    t.assert(conn_old:ping({timeout = 0.5}))
    local new_master_uri = update_uri_param(g.mut_net_box_uri, "after")
    g.server:update_box_cfg({listen = {g.server.net_box_uri, new_master_uri}})
    t.assert(conn_old:ping({timeout = 0.5}))
    local conn_new = net_box.connect(new_master_uri)
    t.assert(conn_new:ping({timeout = 0.5}))
    g.server:exec(function()
        box.iproto.internal.drop_old_connections(0.1)
    end)
    t.assert_not(conn_old:ping({timeout = 0.5}))
    t.assert(conn_new:ping({timeout = 0.5}))
end
