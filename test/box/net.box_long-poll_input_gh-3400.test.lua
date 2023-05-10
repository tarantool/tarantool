test_run = require('test_run').new()
fiber = require 'fiber'
net = require('net.box')

--
-- gh-3400: long-poll input discard must not touch event loop of
-- a closed connection.
--
function long() fiber.yield() return 100 end
c = net.connect(box.cfg.listen)
c:ping()
-- Create batch of two requests. First request is sent to TX
-- thread, second one terminates connection. The preceeding
-- request discards input, and this operation must not trigger
-- new attempts to read any data - the connection is closed
-- already.
--
result = nil
test_run:cmd("setopt delimiter ';'")
f = fiber.create(function()
    _, result = pcall(c._request, c, net._method.call,
                      nil, nil, nil, 'long', {})
end)
pcall(c._request, c, net._method.inject, nil, nil, nil, '\x80')
test_run:cmd("setopt delimiter ''");
test_run:wait_cond(function() return f:status() == 'dead' end)
assert(tostring(result) == 'Peer closed')
c:close()
