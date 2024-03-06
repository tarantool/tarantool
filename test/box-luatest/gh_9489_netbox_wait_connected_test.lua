local fiber = require('fiber')
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

-- Tests that the `wait_connected = false` option of `connect` guarantees that
-- the a yield does not happen, i.e., that `connect` is fully asynchronous.
g.test_wait_connected_fully_async = function(cg)
    local csw_before = fiber.self().csw()
    net_box.connect(cg.server.net_box_uri, {wait_connected = false})
    t.assert_equals(fiber.self().csw(), csw_before)
end
