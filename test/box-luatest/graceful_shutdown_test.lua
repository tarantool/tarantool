local fiber = require('fiber')
local net = require('net.box')
local popen = require('popen')
local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
end)

g.after_all(function()
    g.server:stop()
end)

g.before_each(function()
    if not g.server.process then
        g.server:start()
    end
end)

-- Checks that inprogress requests are completed on shutdown.
g.test_inprogress_requests_completed = function()
    g.server:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'cond', fiber.cond())
        rawset(_G, 'func', function()
            box.session.push(true)
            _G.cond:wait()
            fiber.sleep(0.01)
            return true
        end)
        box.ctl.set_on_shutdown_timeout(9000)
        box.ctl.on_shutdown(function()
            fiber.sleep(0.01)
            _G.cond:broadcast()
        end)
    end)
    local conn = net.connect(g.server.net_box_uri)
    local futures = {}
    for _ = 1, 10 do
        local fut = conn:call('func', {}, {is_async = true})
        -- Wait for the server to invoke the call.
        for _, v in fut:pairs() do -- luacheck: ignore
            t.assert_equals(v, true)
            break
        end
        table.insert(futures, fut)
    end
    g.server:stop()
    for _, fut in ipairs(futures) do
        t.assert(fut:result())
    end
    t.assert_equals(conn.state, 'error')
    t.assert_equals(conn.error, 'Peer closed')
    conn:close()
end

g.before_test('test_discarded_requests_completed', function()
    g.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
    end)
end)

g.after_test('test_discarded_requests_completed', function()
    g.server:exec(function()
        box.space.test:drop()
    end)
end)

-- Checks that discarded requests are completed on shutdown.
g.test_discarded_requests_completed = function()
    g.server:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'cond', fiber.cond())
        rawset(_G, 'func', function()
            box.session.push(true)
            _G.cond:wait()
            fiber.sleep(0.01)
            box.space.test:insert({42})
        end)
        box.ctl.set_on_shutdown_timeout(9000)
        box.ctl.on_shutdown(function()
            fiber.sleep(0.01)
            _G.cond:broadcast()
        end)
    end)
    local conn = net.connect(g.server.net_box_uri)
    local fut = conn:call('func', {}, {is_async = true})
    -- Wait for the server to invoke the call.
    for _, v in fut:pairs() do -- luacheck: ignore
        t.assert_equals(v, true)
        break
    end
    fut:discard()
    g.server:stop()
    local res, err = fut:result()
    t.assert_not(res)
    t.assert_equals(tostring(err), 'Response is discarded')
    t.assert_equals(conn.state, 'error')
    t.assert_equals(conn.error, 'Peer closed')
    conn:close()
    g.server:start()
    g.server:exec(function()
        local t = require('luatest')
        t.assert_equals(box.space.test:get(42), {42})
    end)
end

-- Checks that a hung request doesn't block shutdown.
g.test_hung_requests_aborted = function()
    g.server:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'func', function()
            fiber.sleep(9000)
        end)
        box.ctl.set_on_shutdown_timeout(0.01)
    end)
    local conn = net.connect(g.server.net_box_uri)
    local fut = conn:call('func', {}, {is_async = true})
    g.server:stop()
    local res, err = fut:result()
    t.assert_not(res)
    t.assert_equals(tostring(err), 'Peer closed')
    t.assert_equals(conn.state, 'error')
    t.assert_equals(conn.error, 'Peer closed')
    conn:close()
end

-- Checks that new requests/connections are not allowed during graceful
-- shutdown.
g.test_new_requests_not_allowed = function()
    g.server:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'func', function()
            fiber.sleep(9000)
        end)
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local conn = net.connect(g.server.net_box_uri)
    local fut = conn:call('func', {}, {is_async = true})
    local process = g.server.process
    local on_server_stop = fiber.channel(1)
    fiber.create(function()
        g.server:stop()
        on_server_stop:put(true)
    end)
    t.assert(conn:wait_state('graceful_shutdown'))
    t.assert_error_msg_content_equals(
        'Connection is not established, state is "graceful_shutdown"',
        conn.eval, conn, 'return true')
    local conn2 = net.connect(g.server.net_box_uri)
    t.assert_not(conn2:ping())
    t.assert(conn2.state, 'error')
    t.assert(conn2.error, 'timed out')
    conn2:close()
    process:kill('KILL')
    t.assert(on_server_stop:get())
    t.assert(conn:wait_state('error'))
    t.assert_equals(conn.error, 'Peer closed')
    local res, err = fut:result()
    t.assert_not(res)
    t.assert_equals(tostring(err), 'Peer closed')
    conn:close()
end

-- Checks that a synchronous request sent during graceful shutdown is
-- successfully completed after reconnect.
g.test_sync_request_completed_after_reconnect = function()
    g.server:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'func', function()
            fiber.sleep(9000)
        end)
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local conn = net.connect(g.server.net_box_uri, {reconnect_after = 0.01})
    conn:call('func', {}, {is_async = true})
    t.assert(conn:ping())
    local process = g.server.process
    local on_server_stop = fiber.channel(1)
    fiber.create(function()
        g.server:stop()
        on_server_stop:put(true)
    end)
    t.assert(conn:wait_state('graceful_shutdown'))
    local on_ping_done = fiber.channel(1)
    fiber.create(function()
        on_ping_done:put(conn:ping())
    end)
    t.assert_equals(on_ping_done:get(0.01), nil)
    process:kill('KILL')
    t.assert(on_server_stop:get())
    g.server:start()
    t.assert(on_ping_done:get())
    conn:close()
end

-- Checks that on_shutdown triggers are fired on graceful shutdown.
g.test_triggers_fired = function()
    g.server:exec(function()
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local trigger1_count = 0
    local function trigger1_f()
        trigger1_count = trigger1_count + 1
    end
    local trigger2_count = 0
    local function trigger2_f()
        trigger2_count = trigger2_count + 1
    end
    local trigger3_count = 0
    local function trigger3_f()
        trigger3_count = trigger3_count + 1
    end
    local conn = net.connect(g.server.net_box_uri)
    conn:on_shutdown(trigger1_f)
    conn:on_shutdown(trigger2_f)
    conn:on_shutdown(trigger3_f)
    conn:on_shutdown(nil, trigger3_f)
    t.assert_equals(trigger1_count, 0)
    t.assert_equals(trigger2_count, 0)
    t.assert_equals(trigger3_count, 0)
    g.server:stop()
    t.assert_equals(trigger1_count, 1)
    t.assert_equals(trigger2_count, 1)
    t.assert_equals(trigger3_count, 0)
    conn:close()
end

-- Checks that if a on_shutdown trigger callback raises an error, the client
-- connection will be switched to the 'graceful_shutdown' state and closed once
-- all pending requests have been completed, as usual.
g.test_connection_closed_if_trigger_raises = function()
    g.server:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'cond', fiber.cond())
        rawset(_G, 'wait', function()
            box.session.push(true)
            _G.cond:wait()
            return true
        end)
        rawset(_G, 'signal', function()
            _G.cond:broadcast()
        end)
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local conn1 = net.connect(g.server.net_box_uri)
    local stop_trigger = fiber.channel(1)
    conn1:on_shutdown(function()
        stop_trigger:get()
    end)
    local conn2 = net.connect(g.server.net_box_uri)
    local on_trigger_start = fiber.channel(1)
    conn2:on_shutdown(function()
        on_trigger_start:put(true)
        error('FOOBAR')
    end)
    local fut = conn2:call('wait', {}, {is_async = true})
    -- Wait for the server to invoke the call.
    for _, v in fut:pairs() do -- luacheck: ignore
        t.assert_equals(v, true)
        break
    end
    t.assert_not(on_trigger_start:get(0.01))
    local on_server_stop = fiber.channel(1)
    fiber.create(function()
        g.server:stop()
        on_server_stop:put(true)
    end)
    t.assert(on_trigger_start:get())
    t.assert(conn2:wait_state('graceful_shutdown'))
    conn1:call('signal')
    stop_trigger:put(true)
    t.assert(on_server_stop:get())
    t.assert_equals(conn2.state, 'error')
    t.assert_equals(conn2.error, 'Peer closed')
    t.assert(fut:result())
    conn1:close()
    conn2:close()
end

-- Checks that the connection remains active until on_shutdown triggers return.
g.test_connection_active_while_triggers_running = function()
    g.server:exec(function()
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local on_trigger_start = fiber.channel(1)
    local stop_trigger = fiber.channel(1)
    local function trigger_f()
        on_trigger_start:put(true)
        stop_trigger:get()
    end
    local conn = net.connect(g.server.net_box_uri)
    conn:on_shutdown(trigger_f)
    t.assert(conn:ping())
    local on_server_stop = fiber.channel(1)
    fiber.create(function()
        g.server:stop()
        on_server_stop:put(true)
    end)
    t.assert(on_trigger_start:get())
    t.assert_equals(conn.state, 'active')
    t.assert(conn:eval('return true'))
    stop_trigger:put(true)
    t.assert(on_server_stop:get())
    t.assert_equals(conn.state, 'error')
    t.assert_equals(conn.error, 'Peer closed')
    conn:close()
end

-- Checks that if the server restarts and the client connection is
-- reestablished before on_shutdown triggers return, graceful shutdown
-- won't be started by the client when the triggers finally return.
g.test_shutdown_aborted_on_reconnect = function()
    g.server:exec(function()
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local on_trigger_start = fiber.channel(1)
    local stop_trigger = fiber.channel(1)
    local function trigger_f()
        on_trigger_start:put(true)
        stop_trigger:get()
    end
    local conn = net.connect(g.server.net_box_uri, {reconnect_after = 0.01})
    conn:on_shutdown(trigger_f)
    -- Start shutting down the server and wait for the trigger to run.
    local process = g.server.process
    local on_server_stop = fiber.channel(1)
    fiber.create(function()
        g.server:stop()
        on_server_stop:put(true)
    end)
    t.assert(on_trigger_start:get())
    -- Restart the server and wait for the connection to be reestablished.
    process:kill('KILL')
    t.assert(on_server_stop:get())
    g.server:start()
    t.assert(conn:wait_state('active'))
    -- Complete the trigger and check that the connection isn't broken.
    stop_trigger:put(true)
    t.assert_not(conn:wait_state({'graceful_shutdown', 'error_reconnect'}, 0.1))
    t.assert_equals(conn.state, 'active')
    conn:close()
end

-- Checks that if the server restarts (killed) and the client connection is
-- reestablished and a *new* graceful shutdown is initiated while on_shutdown
-- triggers are still running, triggers won't be started again until the
-- currently running triggers return.
g.test_shutdown_aborted_on_reconnect_2 = function()
    g.server:exec(function()
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local on_trigger_start = fiber.channel(1)
    local stop_trigger = fiber.channel(1)
    local function trigger_f()
        on_trigger_start:put(true)
        stop_trigger:get()
    end
    local conn = net.connect(g.server.net_box_uri, {reconnect_after = 0.01})
    conn:on_shutdown(trigger_f)
    -- Start shutting down the server and wait for the trigger to run.
    local process = g.server.process
    local on_server_stop = fiber.channel(1)
    fiber.create(function()
        g.server:stop()
        on_server_stop:put(true)
    end)
    t.assert(on_trigger_start:get())
    -- Restart the server and wait for the connection to be reestablished.
    process:kill('KILL')
    t.assert(on_server_stop:get())
    g.server:start()
    t.assert(conn:wait_state('active'))
    -- Start shutting down the server and check that the trigger doesn't run.
    fiber.create(function()
        g.server:stop()
        on_server_stop:put(true)
    end)
    t.assert_not(on_trigger_start:get(0.01))
    -- Complete the trigger and check that the connection isn't broken and
    -- the trigger is restarted.
    stop_trigger:put(true)
    t.assert_not(conn:wait_state({'graceful_shutdown', 'error_reconnect'}, 0.1))
    t.assert_equals(conn.state, 'active')
    t.assert(on_trigger_start:get())
    stop_trigger:put(true)
    t.assert(on_server_stop:get())
    t.assert_equals(conn.state, 'error_reconnect')
    conn:close()
end

g.after_test('test_schema_not_fetched_during_shutdown', function()
    if not g.server.process then
        g.server:start()
    end
    g.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Checks that a connection that entered the 'graceful_shutdown' state stops
-- fetching schema.
g.test_schema_not_fetched_during_shutdown = function()
    g.server:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'cond1', fiber.cond())
        rawset(_G, 'wait1', function()
            box.session.push(true)
            _G.cond1:wait()
            return true
        end)
        rawset(_G, 'signal1', function()
            _G.cond1:broadcast()
        end)
        rawset(_G, 'cond2', fiber.cond())
        rawset(_G, 'wait2', function()
            box.session.push(true)
            _G.cond2:wait()
            return true
        end)
        rawset(_G, 'signal2', function()
            _G.cond2:broadcast()
        end)
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local conn1 = net.connect(g.server.net_box_uri)
    local on_trigger_start = fiber.channel(1)
    local stop_trigger = fiber.channel(1)
    conn1:on_shutdown(function()
        on_trigger_start:put(true)
        stop_trigger:get()
    end)
    local conn2 = net.connect(g.server.net_box_uri)
    local schema_reloaded = false
    conn2:on_schema_reload(function()
        schema_reloaded = true
    end)
    local fut1 = conn2:call('wait1', {}, {is_async = true})
    -- Wait for the server to invoke the call.
    for _, v in fut1:pairs() do -- luacheck: ignore
        t.assert_equals(v, true)
        break
    end
    local fut2 = conn2:call('wait2', {}, {is_async = true})
    -- Wait for the server to invoke the call.
    for _, v in fut2:pairs() do -- luacheck: ignore
        t.assert_equals(v, true)
        break
    end
    local on_server_stop = fiber.channel(1)
    fiber.create(function()
        g.server:stop()
        on_server_stop:put(true)
    end)
    t.assert(on_trigger_start:get())
    t.assert(conn1.state, 'active')
    t.assert(conn2:wait_state('graceful_shutdown'))
    conn1:eval([[box.schema.space.create('test')]])
    conn1:call('signal1')
    fiber.sleep(0.01)
    t.assert(conn1.space.test)
    t.assert_not(conn2.space.test)
    t.assert_not(schema_reloaded)
    conn1:eval([[box.space.test:drop()]])
    conn1:call('signal2')
    fiber.sleep(0.01)
    t.assert_not(conn1.space.test)
    t.assert_not(conn2.space.test)
    t.assert_not(schema_reloaded)
    stop_trigger:put(true)
    t.assert(on_server_stop:get())
    t.assert(fut1:result())
    t.assert(fut2:result())
    conn1:close()
    conn2:close()
end

-- Checks that if a client didn't subscribe to the shutdown event, the server
-- won't wait for it to close the connection.
g.test_graceful_shutdown_not_supported_by_client = function()
    g.server:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'func', function()
            box.session.push(true)
            fiber.sleep(9000)
            return true
        end)
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local conn = net.connect(g.server.net_box_uri, {
        _disable_graceful_shutdown = true,
    })
    local fut = conn:call('func', {}, {is_async = true})
    -- Wait for the server to invoke the call.
    for _, v in fut:pairs() do -- luacheck: ignore
        t.assert_equals(v, true)
        break
    end
    g.server:stop()
    local res, err = fut:result()
    t.assert_not(res)
    t.assert_equals(tostring(err), 'Peer closed')
    conn:close()
end

-- Checks that if the net.box callback, which processes box.shutdown event, is
-- garbage collected while the remote isn't, such partially garbage collected
-- connection won't block shutdown (gh-7225).
g.test_net_box_callback_garbage_collected = function()
    g.server:exec(function()
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local conn = net.connect(g.server.net_box_uri)
    t.assert_is_not(conn._callback, nil)
    -- Simulate a situation when the 'conn' reference was lost, the garbage
    -- collector collected 'conn._callback', but hasn't collected the 'conn'
    -- object yet.
    conn._callback = nil
    collectgarbage('collect')
    g.server:stop()
    conn:close()
end

-- Checks that the client closes the connection socket fd even if there is
-- a child processes that shares it (gh-6820).
g.test_graceful_shutdown_and_fork = function()
    g.server:exec(function()
        box.ctl.set_on_shutdown_timeout(9000)
    end)
    local c = net.connect(g.server.net_box_uri)
    local p = popen.new({'/usr/bin/sleep', '9000'}, {close_fds = false})
    t.assert(p)
    c:close()
    g.server:stop()
    p:close()
end
