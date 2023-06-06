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

-- Watchers can be used without box.cfg.
g.test_no_box_cfg = function()
    local count = 0
    local w = box.watch('foo', function() count = count + 1 end)
    t.helpers.retrying({}, function() t.assert_equals(count, 1) end)
    box.broadcast('foo')
    t.helpers.retrying({}, function() t.assert_equals(count, 2) end)
    w:unregister()
    t.assert_equals(box.watch_once('foo'), nil)
end

-- Invalid arguments.
g.test_invalid_args = function(cg)
    cg.server:exec(function()
        t.assert_error_msg_equals("Usage: box.watch(key, function)",
                                  box.watch)
        t.assert_error_msg_equals("Usage: box.watch(key, function)",
                                  box.watch, 1, 2, 3)
        t.assert_error_msg_equals(
            "bad argument #1 to '?' (string expected, got table)",
            box.watch, {}, {})
        t.assert_error_msg_equals(
            "bad argument #2 to '?' (function expected, got table)",
            box.watch, 'foo', {})
        t.assert_error_msg_equals("Usage: box.broadcast(key[, value])",
                                  box.broadcast)
        t.assert_error_msg_equals("Usage: box.broadcast(key[, value])",
                                  box.broadcast, 1, 2, 3)
        t.assert_error_msg_equals(
            "bad argument #1 to '?' (string expected, got table)",
            box.broadcast, {})
        t.assert_error_msg_equals("Usage: box.watch_once(key)",
                                  box.watch_once)
        t.assert_error_msg_equals("Usage: box.watch_once(key)",
                                  box.watch_once, 'a', 'b')
        t.assert_error_msg_equals(
            "bad argument #1 to '?' (string expected, got table)",
            box.watch_once, {})
    end)
end

-- Basic watch/broadcast checks.
g.test_basic = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local count = 0
        local key, value
        local w = box.watch('foo', function(k, v)
            count = count + 1
            key = k
            value = v
        end)
        t.assert_equals(tostring(w), 'box.watcher')
        -- Callback is invoked after registration.
        t.helpers.retrying({}, function() t.assert_equals(count, 1) end)
        t.assert_equals(key, 'foo')
        t.assert_equals(value, nil)
        -- Callback is invoked on broadcast.
        box.broadcast('foo', 123)
        t.helpers.retrying({}, function() t.assert_equals(count, 2) end)
        t.assert_equals(key, 'foo')
        t.assert_equals(value, 123)
        -- Passing nil/none to broadcast.
        box.broadcast('foo', nil)
        t.helpers.retrying({}, function() t.assert_equals(count, 3) end)
        t.assert_equals(key, 'foo')
        t.assert_equals(value, nil)
        box.broadcast('foo')
        t.helpers.retrying({}, function() t.assert_equals(count, 4) end)
        t.assert_equals(key, 'foo')
        t.assert_equals(value, nil)
        -- Callback isn't invoked when another key is updated.
        box.broadcast('bar')
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
    end)
end

g.after_test('test_basic', function(cg)
    cg.server:exec(function()
        box.broadcast('foo')
    end)
end)

g.test_once = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.watch_once('foo'), nil)
        t.assert_equals(box.watch_once('bar'), nil)
        box.broadcast('foo', {1, 2, 3})
        t.assert_equals(box.watch_once('foo'), {1, 2, 3})
        t.assert_equals(box.watch_once('bar'), nil)
        box.broadcast('bar', 'baz')
        t.assert_equals(box.watch_once('foo'), {1, 2, 3})
        t.assert_equals(box.watch_once('bar'), 'baz')
        box.broadcast('foo')
        t.assert_equals(box.watch_once('foo'), nil)
        t.assert_equals(box.watch_once('bar'), 'baz')
    end)
end

g.after_test('test_once', function(cg)
    cg.server:exec(function()
        box.broadcast('foo')
        box.broadcast('bar')
    end)
end)

-- Callback is garbage collected.
g.test_callback_gc = function(cg)
    cg.server:exec(function()
        local cb = function() end
        local weak = setmetatable({cb = cb}, {__mode = 'v'})
        local w = box.watch('foo', cb)
        w:unregister()
        cb = nil -- luacheck: ignore
        t.helpers.retrying({}, function()
            collectgarbage('collect')
            t.assert_equals(weak.cb, nil)
        end)
    end)
end

-- Raising error from a callback.
g.test_callback_error = function(cg)
    cg.server:exec(function()
        local w = box.watch('foo', function() error('foobar') end)
        rawset(_G, 'watcher', w)
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('foobar'))
    end)
end

g.after_test('test_callback_error', function(cg)
    cg.server:exec(function()
        local w = rawget(_G, 'watcher')
        if w then
            w:unregister()
            rawset(_G, 'watcher', nil)
        end
    end)
end)

-- Broadcasting data that can't be encoded in msgpack.
g.test_decode_error = function(cg)
    -- Broadcasting data that can't be encoded in msgpack.
    cg.server:exec(function()
        require('msgpack').cfg({decode_invalid_numbers = false})
        box.broadcast('bar', math.huge)
        local w = box.watch('bar', function() end)
        rawset(_G, 'watcher', w)
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('number must not be NaN or Inf'))
    end)
end

g.after_test('test_decode_error', function(cg)
    cg.server:exec(function()
        local w = rawget(_G, 'watcher')
        if w then
            w:unregister()
            rawset(_G, 'watcher', nil)
        end
        require('msgpack').cfg({decode_invalid_numbers = true})
        box.broadcast('bar')
    end)
end)

-- Several watchers can be registered for the same key.
g.test_multiple_watchers = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local count1 = 0
        local count2 = 0
        local count3 = 0
        local w1 = box.watch('foo', function() count1 = count1 + 1 end)
        local w2 = box.watch('foo', function() count2 = count2 + 1 end)
        local w3 = box.watch('foo', function() count3 = count3 + 1 end)
        t.helpers.retrying({}, function() t.assert_equals(count1, 1) end)
        t.helpers.retrying({}, function() t.assert_equals(count2, 1) end)
        t.helpers.retrying({}, function() t.assert_equals(count3, 1) end)
        w1:unregister()
        box.broadcast('foo')
        fiber.sleep(0.01)
        t.assert_equals(count1, 1)
        t.helpers.retrying({}, function() t.assert_equals(count2, 2) end)
        t.helpers.retrying({}, function() t.assert_equals(count3, 2) end)
        w2:unregister()
        box.broadcast('foo')
        fiber.sleep(0.01)
        t.assert_equals(count1, 1)
        t.assert_equals(count2, 2)
        t.helpers.retrying({}, function() t.assert_equals(count3, 3) end)
        w3:unregister()
    end)
end

-- Watcher is rescheduled if the key is updated while it's running.
g.test_reschedule = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local ch1 = fiber.channel()
        local ch2 = fiber.channel()
        local key = nil
        local value = nil
        box.broadcast('foo', 'bar')
        local w = box.watch('foo', function(k, v)
            key = k value = v
            ch1:put(true)
            ch2:get()
        end)
        t.assert(ch1:get())
        t.assert_equals(key, 'foo')
        t.assert_equals(value, 'bar')
        box.broadcast('foo', 'baz')
        ch2:put(true)
        t.assert(ch1:get())
        t.assert_equals(key, 'foo')
        t.assert_equals(value, 'baz')
        ch2:put(true)
        w:unregister()
    end)
end

g.after_test('test_reschedule', function(cg)
    cg.server:exec(function()
        box.broadcast('foo')
    end)
end)

-- Unregistering a running watcher.
g.test_unregister_running = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local ch = fiber.channel()
        local count = 0
        local w = box.watch('foo', function()
            count = count + 1
            ch:get()
        end)
        t.helpers.retrying({}, function() t.assert_equals(count, 1) end)
        box.broadcast('foo')
        w:unregister();
        ch:put(true)
        fiber.sleep(0.01)
        t.assert_equals(count, 1)
    end)
end

-- Yielding watcher doesn't block other watchers.
g.test_yield = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local ch = fiber.channel()
        local count1 = 0
        local count2 = 0
        local w1 = box.watch('foo', function()
            count1 = count1 + 1
        end)
        local w2 = box.watch('foo', function()
            count2 = count2 + 1
            ch:get()
        end)
        t.helpers.retrying({}, function() t.assert_equals(count1, 1) end)
        t.helpers.retrying({}, function() t.assert_equals(count2, 1) end)
        box.broadcast('foo')
        t.helpers.retrying({}, function() t.assert_equals(count1, 2) end)
        t.assert_equals(count2, 1)
        ch:put(true)
        t.helpers.retrying({}, function() t.assert_equals(count2, 2) end)
        t.assert_equals(count1, 2)
        w1:unregister()
        w2:unregister()
    end)
end

-- Watcher is not unregistered if the handle is garbage collected.
g.test_watcher_gc = function(cg)
    cg.server:exec(function()
        local count = 0
        local w = box.watch('foo', function() count = count + 1 end)
        t.helpers.retrying({}, function() t.assert_equals(count, 1) end)
        local weak = setmetatable({w = w}, {__mode = 'v'})
        w = nil -- luacheck: ignore
        collectgarbage('collect')
        t.assert_equals(weak.w, nil)
        box.broadcast('foo')
        t.helpers.retrying({}, function() t.assert_equals(count, 2) end)
    end)
end
