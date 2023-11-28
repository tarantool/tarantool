local fiber = require('fiber')
local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.before_test('test_iproto_watch_not_reported', function()
    g.server:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_FLIP_FEATURE', 3)
    end)
end)

g.after_test('test_iproto_watch_not_reported', function()
    g.server:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_FLIP_FEATURE', -1)
    end)
end)

-- Checks that if the server doesn't report the 'watchers' feature, the client
-- won't send IPROTO_WATCH and IPROTO_UNWATCH.
g.test_iproto_watch_not_reported = function()
    local conn = net.connect(g.server.net_box_uri)
    t.assert_not_covers(conn.peer_protocol_features, {watchers = true})
    local event_count = 0
    local watcher = conn:watch('foo', function()
        event_count = event_count + 1
    end)
    fiber.sleep(0.01)
    watcher:unregister()
    fiber.sleep(0.01)
    conn:close()
    t.assert_equals(event_count, 0)
end

g.before_test('test_iproto_watch_reported_but_not_implemented', function()
    g.server:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_DISABLE_WATCH', true)
    end)
end)

g.after_test('test_iproto_watch_reported_but_not_implemented', function()
    g.server:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_DISABLE_WATCH', false)
    end)
end)

-- Checks that the client properly handles the case when the server reports
-- the 'watchers' feature as available, but actually doesn't support it.
g.test_iproto_watch_reported_but_not_implemented = function()
    local err_count_1 = g.server:exec(function()
        return box.stat.ERROR.total
    end)
    local conn = net.connect(g.server.net_box_uri)
    t.assert_covers(conn.peer_protocol_features, {watchers = true})
    local event_count = 0
    local watcher = conn:watch('foo', function()
        event_count = event_count + 1
    end)
    fiber.sleep(0.01)
    watcher:unregister()
    g.server:exec(function(err_count_1)
        t.helpers.retrying({}, function()
            local err_count_2 = box.stat.ERROR.total
            t.assert_equals(err_count_2 - err_count_1, 3)
        end)
    end, {err_count_1})
    conn:close()
    t.assert_equals(event_count, 0)
end
