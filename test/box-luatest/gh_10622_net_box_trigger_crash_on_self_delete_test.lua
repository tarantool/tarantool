local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- Reproducer from the issue
g.test_net_box_trigger_crash_on_self_delete = function()
    g.server:exec(function(uri)
        local f = function(conn) conn:on_connect{name = 'test'} end
        local net = require('net.box')
        local c = net.connect(uri, {wait_connected = false})
        c:on_connect{func = f, name = 'test'}
        t.assert_not(c:is_connected())
        t.assert(c:wait_connected(10))
    end, {g.server.net_box_uri})
end

-- Check if Lua trigger list doesn't crash on self delete
g.test_trigger_list_crash_on_self_delete = function()
    g.server:exec(function()
        local trigger_list = require('internal.trigger').new()
        local function f()
            trigger_list(nil, f)
        end
        trigger_list(f)
        trigger_list:run()
    end)
end

-- Check if Lua trigger list doesn't crash when one trigger
-- clears the list
g.test_trigger_list_crash_on_list_clear = function()
    g.server:exec(function()
        local trigger_list = require('internal.trigger').new()
        local function f()
            trigger_list(nil, nil, 't1')
            trigger_list(nil, nil, 't2')
        end
        trigger_list(f, nil, 't1')
        trigger_list(f, nil, 't2')
        trigger_list:run()
    end)
end
