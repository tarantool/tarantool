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
f = fiber.create(c._transport.perform_request, nil, nil, false,         \
                 net._method.call_17, nil, nil, nil, nil, 'long', {})   \
c._transport.perform_request(nil, nil, false, net._method.inject,       \
                             nil, nil, nil, nil, '\x80')
while f:status() ~= 'dead' do fiber.sleep(0.01) end
c:close()
