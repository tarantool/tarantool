local fiber = require('fiber')
local net = require('net.box')
local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all = function()
    g.server = server:new({alias = 'master'})
end

g.after_all = function()
    g.server:stop()
end

g.before_each(function()
    if not g.server.process then
        g.server:start()
    end
end)

g.after_test('test_graceful_shutdown_not_supported_by_client', function()
    box.error.injection.set('ERRINJ_NETBOX_FLIP_FEATURE', -1)
end)

-- Checks that if the graceful shutdown feature bit is not set by a client,
-- the server won't send a SHUTDOWN packet to it or wait for its requests to
-- complete.
g.test_graceful_shutdown_not_supported_by_client = function()
    g.server:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'func', function()
            fiber.sleep(9000)
        end)
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local trigger_fired = false
    local function trigger_f()
        trigger_fired = true
    end
    box.error.injection.set('ERRINJ_NETBOX_FLIP_FEATURE', 4)
    local conn = net.connect(g.server.net_box_uri)
    box.error.injection.set('ERRINJ_NETBOX_FLIP_FEATURE', -1)
    conn:on_shutdown(trigger_f)
    local fut = conn:call('func', {}, {is_async = true})
    g.server:stop()
    t.assert(conn:wait_state('error'))
    t.assert_equals(conn.error, 'Peer closed')
    local res, err = fut:result()
    t.assert_not(res)
    t.assert_equals(tostring(err), 'Peer closed')
    t.assert_not(trigger_fired)
    conn:close()
end

g.after_test('test_id_after_shutdown', function()
    box.error.injection.set('ERRINJ_NETBOX_ID_DELAY', false)
end)

-- Checks that if IPROTO_ID request enabling the graceful shutdown support is
-- sent after the server started shutting down, the client will still be sent
-- a shutdown packet and the net.box connection will be closed right away,
-- without invoking shutdown triggers.
g.test_id_after_shutdown = function()
    g.server:exec(function()
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local conn1, conn2
    local on_trigger1_start = fiber.channel(1)
    local trigger1_resume = fiber.channel(1)
    local function trigger1_f()
        on_trigger1_start:put(true)
        trigger1_resume:get()
    end
    local trigger2_fired = false
    local function trigger2_f()
        trigger2_fired = true
    end
    conn1 = net.connect(g.server.net_box_uri)
    conn1:on_shutdown(trigger1_f)
    box.error.injection.set('ERRINJ_NETBOX_ID_DELAY', true)
    conn2 = net.connect(g.server.net_box_uri, {wait_connected = false})
    conn2:on_shutdown(trigger2_f)
    fiber.sleep(0.01)
    local on_server_stop = fiber.channel(1)
    fiber.create(function()
        g.server:stop()
        on_server_stop:put(true)
    end)
    t.assert(on_trigger1_start:get())
    box.error.injection.set('ERRINJ_NETBOX_ID_DELAY', false)
    t.assert(conn2:wait_state('error'))
    t.assert_equals(conn2.state, 'error')
    t.assert_equals(conn2.error, 'Peer closed')
    t.assert_not(trigger2_fired)
    trigger1_resume:put(true)
    t.assert(on_server_stop:get())
    conn1:close()
    conn2:close()
end

g.after_test('test_auth_after_shutdown', function()
    box.error.injection.set('ERRINJ_NETBOX_AUTH_DELAY', false)
    if not g.server.process then
        g.server:start()
    end
    g.server:exec(function()
        box.schema.user.drop('test', {if_exists = true})
    end)
end)

-- Checks that if IPROTO_AUTH request is sent after the server started shutting
-- down, the net.box connection will be closed right away, without invoking
-- shutdown triggers.
g.test_auth_after_shutdown = function()
    g.server:exec(function()
        box.ctl.set_on_shutdown_timeout(9000)
        box.schema.user.create('test')
        box.schema.user.passwd('test', 'test')
    end)
    local conn1, conn2
    local on_trigger1_start = fiber.channel(1)
    local trigger1_resume = fiber.channel(1)
    local function trigger1_f()
        on_trigger1_start:put(true)
        trigger1_resume:get()
    end
    local trigger2_fired = false
    local function trigger2_f()
        trigger2_fired = true
    end
    conn1 = net.connect(g.server.net_box_uri)
    conn1:on_shutdown(trigger1_f)
    box.error.injection.set('ERRINJ_NETBOX_AUTH_DELAY', true)
    conn2 = net.connect(g.server.net_box_uri, {
        user = 'test',
        password = 'test',
        wait_connected = false,
    })
    conn2:on_shutdown(trigger2_f)
    t.assert(conn2:wait_state('auth'))
    local on_server_stop = fiber.channel(1)
    fiber.create(function()
        g.server:stop()
        on_server_stop:put(true)
    end)
    t.assert(on_trigger1_start:get())
    box.error.injection.set('ERRINJ_NETBOX_AUTH_DELAY', false)
    t.assert(conn2:wait_state('error'))
    t.assert_equals(conn2.state, 'error')
    t.assert_equals(conn2.error, 'Peer closed')
    t.assert_not(trigger2_fired)
    trigger1_resume:put(true)
    t.assert(on_server_stop:get())
    conn1:close()
    conn2:close()
end
