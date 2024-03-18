local net_box = require('net.box')
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

-- Tests that throwing an error from the `on_disconnect` trigger does not hang
-- the server indefinitely.
g.test_on_disconnect_error_hangs_server = function(cg)
    local c = net_box.connect(cg.server.net_box_uri)
    c:on_disconnect(function()
        error(777)
    end)
    pcall(function() c:close() end)
    local log_file = cg.server:exec(function()
        box.ctl.set_on_shutdown_timeout(1)
        -- `grep_log` will not be able to retrieve it after we drop the server.
        return box.cfg.log
    end)
    cg.server:drop()
    t.assert_not(cg.server:grep_log('on_shutdown triggers failed', 1024,
                                    {filename = log_file}))
end
