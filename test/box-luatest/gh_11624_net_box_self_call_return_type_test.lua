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

local test_net_box_call_types_template = function(cg, option, expected_type)
    cg.server:exec(function(option, expected_type)
        local net_box = require('net.box')
        local compat = require('compat')
        compat.box_tuple_extension = option
        local listen_uri = box.cfg.listen
        local conn_self = net_box.self

        local conn_remote = net_box.connect(listen_uri)
        local res_remote = conn_remote:call('return_tuple')
        local res_self = conn_self:call('return_tuple')
        t.assert_equals(type(res_self), type(res_remote))
        t.assert_equals(type(res_self), expected_type)
        conn_remote:close()
    end, {option, expected_type})
end

g.test_net_box_self_tuple_types = function(cg)
    test_net_box_call_types_template(cg, 'new', 'cdata')
    test_net_box_call_types_template(cg, 'old', 'table')
end
