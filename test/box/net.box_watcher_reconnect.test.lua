test_run = require('test_run').new()
fiber = require('fiber')
net = require('net.box')
errinj = box.error.injection

--
-- Check that net.box watchers are resubscribed after reconnect.
--
timeout = 0.01
conn = net.connect(box.cfg.listen, {reconnect_after = timeout})

count = 0
key = nil
value = nil
w = conn:watch('foo', function(k, v) count = count + 1 key = k value = v end)
test_run:wait_cond(function() return count == 1 end)
assert(count == 1)
assert(key == 'foo')
assert(value == nil)

errinj.set('ERRINJ_NETBOX_IO_ERROR', true)
conn:ping({timeout = timeout})
conn.error
box.broadcast('foo', 'bar')
fiber.sleep(timeout)
assert(count == 1)
errinj.set('ERRINJ_NETBOX_IO_ERROR', false)
test_run:wait_cond(function() return count == 2 end)
assert(count == 2)
assert(key == 'foo')
assert(value == 'bar')

box.broadcast('foo', 'baz')
test_run:wait_cond(function() return count == 3 end)
assert(count == 3)
assert(key == 'foo')
assert(value == 'baz')

conn:close()
box.broadcast('foo')
