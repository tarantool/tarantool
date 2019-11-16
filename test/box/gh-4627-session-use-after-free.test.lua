--
-- gh-4627: binary session disconnect trigger yield could lead to
-- use after free of the session object. That happened because
-- iproto thread sent two requests to TX thread at disconnect:
--
--     - Close the session and run its on disconnect triggers;
--
--     - If all requests are handled, destroy the session.
--
-- When a connection is idle, all requests are handled, so both
-- these requests are sent. If the first one yielded in TX thread,
-- the second one arrived and destroyed the session right under
-- the feet of the first one.
--
net_box = require('net.box')
fiber = require('fiber')

sid_before_yield = nil
sid_after_yield = nil
func = box.session.on_disconnect(function()     \
    sid_before_yield = box.session.id()         \
    fiber.yield()                               \
    sid_after_yield = box.session.id()          \
end)

connection = net_box.connect(box.cfg.listen)
connection:ping()
connection:close()

while not sid_after_yield do fiber.yield() end

sid_after_yield == sid_before_yield and sid_after_yield ~= 0 or \
    {sid_after_yield, sid_before_yield}

box.session.on_disconnect(nil, func)
