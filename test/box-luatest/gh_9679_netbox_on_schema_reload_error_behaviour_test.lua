local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Tests the behaviour of the `on_schema_reload` trigger.
g.test_on_schema_reload_error_behaviour = function(cg)
    local trigger_err = '777'
    cg.server:exec(function(trigger_err)
        local net_box = require('net.box')

        local c = net_box.connect(box.cfg.listen, {wait_connected = false})
        c:on_schema_reload(function()
            error(trigger_err)
        end)
        t.assert(c:wait_state('active', 10))
    end, {trigger_err})
    t.assert(cg.server:grep_log(trigger_err))
end
