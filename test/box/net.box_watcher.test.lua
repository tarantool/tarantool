test_run = require('test_run').new()
fiber = require('fiber')
msgpack = require('msgpack')
net = require('net.box')

conn = net.connect(box.cfg.listen)

-- Invalid arguments.
conn.watch()
conn:watch(123, function() end)
conn:watch('abc', 123)

-- Callback is invoked after registration.
count = 0
key = nil
value = nil
cb = function(k, v) count = count + 1 key = k value = v end
w = conn:watch('foo', cb)
assert(tostring(w) == 'net.box.watcher')
test_run:wait_cond(function() return count == 1 end)
assert(count == 1)
assert(key == 'foo')
assert(value == nil)

-- Callback is invoked on broadcast.
count = 0
box.broadcast('foo', 123)
test_run:wait_cond(function() return count == 1 end)
assert(count == 1)
assert(key == 'foo')
assert(value == 123)

-- Passing nil/none to broadcast.
count = 0
box.broadcast('foo', nil)
test_run:wait_cond(function() return count == 1 end)
assert(count == 1)
assert(key == 'foo')
assert(value == nil)
count = 0
box.broadcast('foo')
test_run:wait_cond(function() return count == 1 end)
assert(count == 1)
assert(key == 'foo')
assert(value == nil)

-- Callback isn't invoked when another key is updated.
count = 0
box.broadcast('bar')
fiber.sleep(0.01)
assert(count == 0)

-- Callback isn't invoked after unregistration.
count = 0
w:unregister()
box.broadcast('foo')
fiber.sleep(0.01)
assert(count == 0)

-- Second attempt to unregister fails.
w:unregister()

-- Raising error from a callback.
w = conn:watch('foobar', function() error('foobar') end)
test_run:wait_log('default', 'foobar')
w:unregister()

-- Broadcasting data that can't be encoded in msgpack.
msgpack.cfg({decode_invalid_numbers = false})
box.broadcast('foobad', math.huge)
count = 0
w = conn:watch('foobad', function() count = count + 1 end)
test_run:wait_cond(function() return conn.state == 'error' end)
assert(string.match(conn.error, 'number must not be NaN or Inf'))
conn:close()
assert(count == 0)
w:unregister()
msgpack.cfg({decode_invalid_numbers = true})
conn = net.connect(box.cfg.listen)

-- Several watchers can be registered for the same key.
count1 = 0
count2 = 0
count3 = 0
w1 = conn:watch('foofoo', function() count1 = count1 + 1 end)
test_run:wait_cond(function() return count1 == 1 end)
w2 = conn:watch('foofoo', function() count2 = count2 + 1 end)
test_run:wait_cond(function() return count2 == 1 end)
w3 = conn:watch('foofoo', function() count3 = count3 + 1 end)
test_run:wait_cond(function() return count3 == 1 end)
assert(count1 == 1)
assert(count2 == 1)
assert(count3 == 1)
w1:unregister()
box.broadcast('foofoo')
test_run:wait_cond(function() return count2 == 2 end)
test_run:wait_cond(function() return count3 == 2 end)
assert(count1 == 1)
assert(count2 == 2)
assert(count3 == 2)
w2:unregister()
box.broadcast('foofoo')
test_run:wait_cond(function() return count3 == 3 end)
assert(count1 == 1)
assert(count2 == 2)
assert(count3 == 3)
w3:unregister()

-- Watcher is rescheduled if the key is updated while it's running.
ch1 = fiber.channel()
ch2 = fiber.channel()
key = nil
value = nil
box.broadcast('fez', 'bar')
w = conn:watch('fez', function(k, v) key = k value = v ch1:put(true) ch2:get() end)
ch1:get()
assert(key == 'fez')
assert(value == 'bar')
box.broadcast('fez', 'baz')
ch2:put(true)
ch1:get()
assert(key == 'fez')
assert(value == 'baz')
ch2:put(true)
w:unregister()
box.broadcast('fez')

-- Unregistering a running watcher.
count = 0
ch = fiber.channel()
w = conn:watch('abc', function() count = count + 1 ch:get() end)
test_run:wait_cond(function() return count == 1 end)
assert(count == 1)
box.broadcast('abc')
w:unregister()
ch:put(true)
fiber.sleep(0.01)
assert(count == 1)

-- Yielding watcher doesn't block other watchers.
count1 = 0
count2 = 0
ch = fiber.channel()
w1 = conn:watch('test', function() count1 = count1 + 1 end)
w2 = conn:watch('test', function() count2 = count2 + 1 ch:get() end)
test_run:wait_cond(function() return count1 == 1 end)
test_run:wait_cond(function() return count2 == 1 end)
assert(count1 == 1)
assert(count2 == 1)
box.broadcast('test')
test_run:wait_cond(function() return count1 == 2 end)
assert(count1 == 2)
assert(count2 == 1)
ch:put(true)
test_run:wait_cond(function() return count2 == 2 end)
assert(count1 == 2)
assert(count2 == 2)
w1:unregister()
w2:unregister()

-- Closing connection while there are active watchers.
count1 = 0
count2 = 0
ch = fiber.channel()
w1 = conn:watch('fuss', function() count1 = count1 + 1 end)
w2 = conn:watch('buzz', function() count2 = count2 + 1 ch:get() end)
test_run:wait_cond(function() return count1 == 1 end)
test_run:wait_cond(function() return count2 == 1 end)
assert(count1 == 1)
assert(count2 == 1)
conn:close()
ch:put(true)
box.broadcast('fuss')
box.broadcast('buzz')
fiber.sleep(0.01)
assert(count1 == 1)
assert(count2 == 1)
