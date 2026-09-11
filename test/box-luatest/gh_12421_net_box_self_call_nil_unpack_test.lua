local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'return_with_nil', function()
            return 1, nil, 3
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_net_box_return_with_nil = function(cg)
    cg.server:exec(function()
        local net_box = require('net.box')
        local listen_uri = box.cfg.listen
        local conn_self = net_box.self

        local conn_remote = net_box.connect(listen_uri)
        local res_remote = {conn_remote:call('return_with_nil')}
        local res_self = {conn_self:call('return_with_nil')}
        conn_remote:close()

        t.assert_equals(res_self, {1, nil, 3})
        t.assert_equals(res_remote, {1, nil, 3})
    end)
end
