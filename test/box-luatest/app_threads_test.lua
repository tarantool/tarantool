local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {
            iproto_threads = 2,
            app_threads = 4,
        },
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_dummy = function()
end
