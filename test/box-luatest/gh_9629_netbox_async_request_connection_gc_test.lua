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

-- Tests that a connection with asynchronous requests does not get garbage
-- collected.
g.test_async_request_connection_gc = function(cg)
    local c = net_box.connect(cg.server.net_box_uri)
    local f = c:eval('return 777', {}, {is_async = true})
    c = nil -- luacheck: no unused
    collectgarbage()
    local res, err = f:wait_result(10)
    t.assert_equals(err, nil)
    t.assert_equals(res[1], 777)
end
