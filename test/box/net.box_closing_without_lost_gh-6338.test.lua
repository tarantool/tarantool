net = require('net.box')
fiber = require('fiber')
test_run = require('test_run').new()

errinj = box.error.injection

box.schema.user.grant('guest', 'execute', 'universe')

--
-- Check that close() sends all pending requests:
--  - Block the net.box worker (with error injection).
--  - Send a few requests.
--  - Start closing the connection (in a fiber, because it blocks).
--  - Try to send another request - should fail now.
--  - Unblock the net.box worker.
--  - Check that all requests have been executed.
--
counter = 0
expected_counter = 5
ch = fiber.channel()
conn = net.connect(box.cfg.listen)
errinj.set("ERRINJ_NETBOX_IO_DELAY", true)
for i = 1, expected_counter do \
    conn:eval('counter = counter + 1', {}, {is_async = true}) \
end
_ = fiber.create(function() conn:close() ch:put(true) end)
assert(conn:eval('ping')) -- fails: connection is closing
errinj.set("ERRINJ_NETBOX_IO_DELAY", false)
ch:get() -- close() returned
test_run:wait_cond(function() return box.stat.net().REQUESTS.current == 0 end)
assert(counter == expected_counter) -- (because all requests have been executed)

--
-- Check that close() doesn't hang on send error.
--  - Block the net.box worker (with error injection).
--  - Send a few requests.
--  - Start closing the connection (in a fiber, because it blocks).
--  - Inject IO error.
--  - Unblock the net.box worker.
--  - Check that close() completed.
--
counter = 0
ch = fiber.channel()
conn = net.connect(box.cfg.listen)
errinj.set("ERRINJ_NETBOX_IO_DELAY", true)
for i = 1, 5 do \
    conn:eval('counter = counter + 1', {}, {is_async = true}) \
end
_ = fiber.create(function() conn:close() ch:put(true) end)
errinj.set("ERRINJ_NETBOX_IO_ERROR", true)
errinj.set("ERRINJ_NETBOX_IO_DELAY", false)
ch:get()
assert(counter == 0) -- (requests were dropped because of IO error)
errinj.set("ERRINJ_NETBOX_IO_ERROR", false)

box.schema.user.revoke('guest', 'execute', 'universe')
