local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'return_tuple', function()
            return box.tuple.new({1, 2, 3})
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_net_box_self_tuple_types = function(cg)
    cg.server:exec(function()
        local net_box = require('net.box')
        local compat = require('compat')
        local listen_uri = box.cfg.listen
        local conn_self = net_box.self

        compat.box_tuple_extension = 'new'
        local conn_remote_new = net_box.connect(listen_uri)
        local res_remote_new = conn_remote_new:call('return_tuple')
        local res_self_new   = conn_self:call('return_tuple')
        t.assert_equals(type(res_self_new), 'cdata')
        t.assert_equals(type(res_self_new), type(res_remote_new))
        conn_remote_new:close()

        compat.box_tuple_extension = 'old'
        local conn_remote_old = net_box.connect(listen_uri)
        local res_remote_old = conn_remote_old:call('return_tuple')
        local res_self_old   = conn_self:call('return_tuple')
        t.assert_equals(type(res_self_old), 'table')
        t.assert_equals(type(res_self_old), type(res_remote_old))
        conn_remote_old:close()
    end)
end
