test_run = require('test_run').new()
fiber = require('fiber')
msgpack = require('msgpack')

-- Invalid arguments.
box.watch()
box.watch(1, 2, 3)
box.watch({}, {})
box.watch('foo', {})
box.broadcast()
box.broadcast(1, 2, 3)
box.broadcast({})

-- Callback is invoked after registration.
count = 0
key = nil
value = nil
cb = function(k, v) count = count + 1 key = k value = v end
w = box.watch('foo', cb)
assert(tostring(w) == 'box.watcher')
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

-- Callback is garbage collected.
weak = setmetatable({cb = cb}, {__mode = 'v'})
cb = nil
_ = collectgarbage()
assert(weak.cb == nil)

-- Raising error from a callback.
w = box.watch('foo', function() error('foobar') end)
test_run:wait_log('default', 'foobar', 1000)
w:unregister()

-- Broadcasting data that can't be encoded in msgpack.
msgpack.cfg({decode_invalid_numbers = false})
box.broadcast('foo', math.huge)
count = 0
w = box.watch('foo', function() count = count + 1 end)
test_run:wait_log('default', 'number must not be NaN or Inf', 1000)
assert(count == 0)
w:unregister()
msgpack.cfg({decode_invalid_numbers = true})

-- Several watchers can be registered for the same key.
box.broadcast('foo')
count1 = 0
count2 = 0
count3 = 0
w1 = box.watch('foo', function() count1 = count1 + 1 end)
test_run:wait_cond(function() return count1 == 1 end)
w2 = box.watch('foo', function() count2 = count2 + 1 end)
test_run:wait_cond(function() return count2 == 1 end)
w3 = box.watch('foo', function() count3 = count3 + 1 end)
test_run:wait_cond(function() return count3 == 1 end)
assert(count1 == 1)
assert(count2 == 1)
assert(count3 == 1)
w1:unregister()
box.broadcast('foo')
test_run:wait_cond(function() return count2 == 2 end)
test_run:wait_cond(function() return count3 == 2 end)
assert(count1 == 1)
assert(count2 == 2)
assert(count3 == 2)
w2:unregister()
box.broadcast('foo')
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
box.broadcast('foo', 'bar')
w = box.watch('foo', function(k, v) key = k value = v ch1:put(true) ch2:get() end)
ch1:get()
assert(key == 'foo')
assert(value == 'bar')
box.broadcast('foo', 'baz')
ch2:put(true)
ch1:get()
assert(key == 'foo')
assert(value == 'baz')
ch2:put(true)
w:unregister()

-- Unregistering a running watcher.
count = 0
ch = fiber.channel()
w = box.watch('foo', function() count = count + 1 ch:get() end)
test_run:wait_cond(function() return count == 1 end)
assert(count == 1)
box.broadcast('foo')
w:unregister();
ch:put(true)
fiber.sleep(0.01)
assert(count == 1)

-- Yielding watcher doesn't block other watchers.
count1 = 0
count2 = 0
ch = fiber.channel()
w1 = box.watch('foo', function() count1 = count1 + 1 end)
w2 = box.watch('foo', function() count2 = count2 + 1 ch:get() end)
test_run:wait_cond(function() return count1 == 1 end)
test_run:wait_cond(function() return count2 == 1 end)
assert(count1 == 1)
assert(count2 == 1)
box.broadcast('foo')
test_run:wait_cond(function() return count1 == 2 end)
assert(count1 == 2)
assert(count2 == 1)
ch:put(true)
test_run:wait_cond(function() return count2 == 2 end)
assert(count1 == 2)
assert(count2 == 2)
w1:unregister()
w2:unregister()

-- Watcher is not unregistered if the handle is garbage collected.
count = 0
w = box.watch('foo', function() count = count + 1 end)
test_run:wait_cond(function() return count == 1 end)
assert(count == 1)
weak = setmetatable({w = w}, {__mode = 'v'})
w = nil
collectgarbage('collect')
assert(weak.w == nil)
count = 0
box.broadcast('foo', 123)
test_run:wait_cond(function() return count == 1 end)
assert(count == 1)
