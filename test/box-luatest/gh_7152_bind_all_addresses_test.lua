local t = require("luatest")
local server = require('luatest.server')

local g = t.group()

g.before_all(function(g)
    local port = 3301
    g.server = server:new({net_box_port = port})
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.test_bind_single_port_all_interfaces = function(g)
    g.server:exec(function()
        local bound_uris = box.info.listen
        t.skip_if(type(bound_uris) == "string" or #bound_uris <= 1)
        local net_box = require('net.box')
        for _, uri in ipairs(bound_uris) do
            local conn = net_box.connect(uri)
            local rc = conn:ping()
            conn:close()
            t.assert(rc)
        end
    end)
end
