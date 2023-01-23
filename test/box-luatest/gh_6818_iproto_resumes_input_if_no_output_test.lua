local fiber = require('fiber')
local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({
        alias = 'master',
        box_cfg = {
            net_msg_max = 4,
        }
    })
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

-- Checks that IPROTO resumes input stopped upon reaching the net_msg_max
-- limit even if nothing was sent on output. This is needed to guarantee
-- that a request that doesn't have a response, such as IPROTO_WATCH, does
-- not prevent a connection from being resumed.
g.test_iproto_resumes_input_if_no_output = function()
    local fiber_count = 100
    local loop_count = 10
    local on_done = fiber.channel(fiber_count)
    for _ = 1, fiber_count do
        fiber.create(function()
            local conn = net.connect(g.server.net_box_uri)
            conn:watch('foo', function() end)
            for _ = 1, loop_count do
                conn:eval([[require('fiber').sleep(0.001)]])
            end
            conn:close()
            on_done:put(true)
        end)
    end
    for _ = 1, fiber_count do
        t.assert(on_done:get(10))
    end
end
