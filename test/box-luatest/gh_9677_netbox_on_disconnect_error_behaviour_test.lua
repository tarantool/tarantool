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

-- Tests that the actual behaviour of the `on_disconnect` trigger when an error
-- is thrown is consistent with its documented behaviour.
g.test_on_disconnect_error_behaviour = function(cg)
    local trigger_err = '777'
    cg.server:exec(function(trigger_err)
        local net_box = require('net.box')

        local c = net_box.connect(box.cfg.listen)
        c:on_disconnect(function()
            error(trigger_err)
        end)
        local ok = pcall(function() c:close() end)
        t.assert(ok)
        t.assert(c:wait_state('closed', 10))
    end, {trigger_err})
    t.assert(cg.server:grep_log(trigger_err))
end
