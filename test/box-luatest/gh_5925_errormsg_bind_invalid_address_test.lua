local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(g)
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.test_listen_address = function(g)
    t.assert_error(function()
        g.server:update_box_cfg({listen = '0.42.42.42:1234'})
    end)
    t.assert(
        g.server:grep_log("aka 0.42.42.42:1234: " ..
                          "Cannot assign requested address", 2048)
    )
end
