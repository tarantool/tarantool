local fiber = require('fiber')
local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.broadcast = function(key, value)
        cg.server:exec(function(k, v)
            box.broadcast(k, v)
        end, {key, value})
    end
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Invalid arguments.
g.test_invalid_args = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    t.assert_error_msg_equals(
        "Use remote:watch(...) instead of remote.watch(...):",
        conn.watch)
    t.assert_error_msg_equals("key must be a string",
                              conn.watch, conn, 123, function() end)
    t.assert_error_msg_equals("func must be a function",
                              conn.watch, conn, 'abc', 123)
    conn:close()
end

-- Basic watch/broadcast checks.
g.test_basic = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    local count = 0
    local key, value
    local w = conn:watch('foo', function(k, v)
        count = count + 1
        key = k
        value = v
    end)
    t.assert_equals(tostring(w), 'net.box.watcher')
    -- Callback is invoked after registration.
    t.helpers.retrying({}, function() t.assert_equals(count, 1) end)
    t.assert_equals(key, 'foo')
    t.assert_equals(value, nil)
    -- Callback is invoked on broadcast.
    cg.broadcast('foo', 123)
    t.helpers.retrying({}, function() t.assert_equals(count, 2) end)
    t.assert_equals(key, 'foo')
    t.assert_equals(value, 123)
    -- Passing nil/none to broadcast.
    cg.broadcast('foo', nil)
    t.helpers.retrying({}, function() t.assert_equals(count, 3) end)
    t.assert_equals(key, 'foo')
    t.assert_equals(value, nil)
    cg.broadcast('foo')
    t.helpers.retrying({}, function() t.assert_equals(count, 4) end)
    t.assert_equals(key, 'foo')
    t.assert_equals(value, nil)
    -- Callback isn't invoked when another key is updated.
    cg.broadcast('bar')
    fiber.sleep(0.01)
    t.assert_equals(count, 4)
    -- Callback isn't invoked after unregistration.
    w:unregister()
    box.broadcast('foo')
    fiber.sleep(0.01)
    t.assert_equals(count, 4)
    -- Second attempt to unregister fails.
    t.assert_error_msg_equals('Watcher is already unregistered',
                              w.unregister, w)
    conn:close()
end

g.after_test('test_basic', function(cg)
    cg.broadcast('foo')
end)

-- Raising error from a callback.
g.test_callback_error = function(cg)
    cg.server:exec(function()
        local conn = require('net.box').connect(box.cfg.listen)
        t.assert(conn)
        t.assert_equals(conn.state, 'active')
        rawset(_G, 'conn', conn)
        conn:watch('foobar', function() error('foobar') end)
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('foobar'))
    end)
end

g.after_test('test_callback_error', function(cg)
    cg.server:exec(function()
        local conn = rawget(_G, 'conn')
        if conn then
            conn:close()
            rawset(_G, 'conn', nil)
        end
    end)
end)

-- Broadcasting data that can't be encoded in msgpack.
g.test_decode_error = function(cg)
    cg.server:exec(function()
        require('msgpack').cfg({decode_invalid_numbers = false})
        box.broadcast('foobad', math.huge)
        local conn = require('net.box').connect(box.cfg.listen)
        t.assert(conn)
        t.assert_equals(conn.state, 'active')
        rawset(_G, 'conn', conn)
        conn:watch('foobad', function() end)
        t.helpers.retrying({}, function()
            t.assert_str_contains(conn.error, 'number must not be NaN or Inf')
        end)
    end)
end

g.after_test('test_decode_error', function(cg)
    cg.server:exec(function()
        local conn = rawget(_G, 'conn')
        if conn then
            conn:close()
            rawset(_G, 'conn', nil)
        end
        require('msgpack').cfg({decode_invalid_numbers = true})
        box.broadcast('foobad')
    end)
end)

-- Several watchers can be registered for the same key.
g.test_multiple_watchers = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    local count1 = 0
    local count2 = 0
    local count3 = 0
    local w1 = conn:watch('foofoo', function() count1 = count1 + 1 end)
    local w2 = conn:watch('foofoo', function() count2 = count2 + 1 end)
    local w3 = conn:watch('foofoo', function() count3 = count3 + 1 end)
    t.helpers.retrying({}, function() t.assert_equals(count1, 1) end)
    t.helpers.retrying({}, function() t.assert_equals(count2, 1) end)
    t.helpers.retrying({}, function() t.assert_equals(count3, 1) end)
    w1:unregister()
    cg.broadcast('foofoo')
    fiber.sleep(0.01)
    t.assert_equals(count1, 1)
    t.helpers.retrying({}, function() t.assert_equals(count2, 2) end)
    t.helpers.retrying({}, function() t.assert_equals(count3, 2) end)
    w2:unregister()
    cg.broadcast('foofoo')
    fiber.sleep(0.01)
    t.assert_equals(count1, 1)
    t.assert_equals(count2, 2)
    t.helpers.retrying({}, function() t.assert_equals(count3, 3) end)
    w3:unregister()
    conn:close()
end

-- Watcher is rescheduled if the key is updated while it's running.
g.test_reschedule = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    local ch1 = fiber.channel()
    local ch2 = fiber.channel()
    local key, value
    cg.broadcast('fez', 'bar')
    local w = conn:watch('fez', function(k, v)
        key = k
        value = v
        ch1:put(true)
        ch2:get()
    end)
    t.assert(ch1:get())
    t.assert_equals(key, 'fez')
    t.assert_equals(value, 'bar')
    cg.broadcast('fez', 'baz')
    ch2:put(true)
    t.assert(ch1:get())
    t.assert_equals(key, 'fez')
    t.assert_equals(value, 'baz')
    ch2:put(true)
    w:unregister()
    conn:close()
end

g.after_test('test_reschedule', function(cg)
    cg.broadcast('fez')
end)

-- Unregistering a running watcher.
g.test_unregister_running = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    local count = 0
    local ch = fiber.channel()
    local w = conn:watch('abc', function()
        count = count + 1
        ch:get()
    end)
    t.helpers.retrying({}, function() t.assert_equals(count, 1) end)
    cg.broadcast('abc')
    w:unregister()
    ch:put(true)
    fiber.sleep(0.01)
    t.assert_equals(count, 1)
    conn:close()
end

-- Yielding watcher doesn't block other watchers.
g.test_yield = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    local count1 = 0
    local count2 = 0
    local ch = fiber.channel()
    local w1 = conn:watch('test', function()
        count1 = count1 + 1
    end)
    local w2 = conn:watch('test', function()
        count2 = count2 + 1
        ch:get()
    end)
    t.helpers.retrying({}, function() t.assert_equals(count1, 1) end)
    t.helpers.retrying({}, function() t.assert_equals(count2, 1) end)
    cg.broadcast('test')
    t.helpers.retrying({}, function() t.assert_equals(count1, 2) end)
    t.assert_equals(count2, 1)
    ch:put(true)
    t.helpers.retrying({}, function() t.assert_equals(count2, 2) end)
    t.assert_equals(count1, 2)
    w1:unregister()
    w2:unregister()
    conn:close()
end

-- Closing connection while there are active watchers.
g.test_close = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    local count1 = 0
    local count2 = 0
    local ch = fiber.channel()
    conn:watch('fuss', function()
        count1 = count1 + 1
    end)
    conn:watch('buzz', function()
        count2 = count2 + 1
        ch:get()
    end)
    t.helpers.retrying({}, function() t.assert_equals(count1, 1) end)
    t.helpers.retrying({}, function() t.assert_equals(count2, 1) end)
    conn:close()
    ch:put(true)
    cg.broadcast('fuss')
    cg.broadcast('buzz')
    fiber.sleep(0.01)
    t.assert_equals(count1, 1)
    t.assert_equals(count2, 1)
end

-- Check that net.box watchers are resubscribed after reconnect.
g.test_reconnect = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local net = require('net.box')
        local fiber = require('fiber')
        local timeout = 0.01
        local conn = net.connect(box.cfg.listen,
                                 {reconnect_after = timeout})
        local count = 0
        local key, value
        local w = conn:watch('foo', function(k, v)
            count = count + 1
            key = k
            value = v
        end)
        t.helpers.retrying({}, function() t.assert_equals(count, 1) end)
        t.assert_equals(key, 'foo')
        t.assert_equals(value, nil)

        box.error.injection.set('ERRINJ_NETBOX_IO_ERROR', true)
        conn:ping({timeout = timeout})
        t.assert_equals(conn.error, 'Error injection')
        box.broadcast('foo', 'bar')
        fiber.sleep(timeout)
        t.assert_equals(count, 1)
        box.error.injection.set('ERRINJ_NETBOX_IO_ERROR', false)
        t.helpers.retrying({}, function() t.assert_equals(count, 2) end)
        t.assert_equals(key, 'foo')
        t.assert_equals(value, 'bar')

        box.broadcast('foo', 'baz')
        t.helpers.retrying({}, function() t.assert_equals(count, 3) end)
        t.assert_equals(key, 'foo')
        t.assert_equals(value, 'baz')

        w:unregister()
        conn:close()
    end)
end

g.after_test('test_reconnect', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_NETBOX_IO_ERROR', false)
        box.broadcast('foo')
    end)
end)
