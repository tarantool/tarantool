local server = require('luatest.server')
local fiber = require('fiber')
local t = require('luatest')

local g = t.group()

local delay_shutdown
local delay_shutdown_cond = fiber.cond()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local fiber = require('fiber')
        local socket = require('socket')
        rawset(_G, 'delay_shutdown', true)
        rawset(_G, 'delay_shutdown_cond', fiber.cond())
        rawset(_G, 'delay_shutdown', function()
            while _G.delay_shutdown do
                _G.delay_shutdown_cond:wait()
            end
        end)
        -- Wait until we refuse to connect which means iproto shutdown trigger
        -- is finished and new connections should not be possible.
        rawset(_G, 'wait_iproto_shutdown', function()
            t.helpers.retrying({}, function()
                local sock, err = socket.tcp_connect(box.cfg.listen)
                if err == nil then
                    sock:close()
                    error('still ok')
                end
            end)
        end)
    end)
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:exec(function()
            _G.delay_shutdown = false
            _G.delay_shutdown_cond:signal()
        end)
        delay_shutdown = false
        delay_shutdown_cond:signal()
        cg.server:drop()
    end
end)

-- Delay shutdown of connection to server so that we can continue to send
-- commands to server after server shutdown started.
local function delay_server_connection_shutdown(server)
    delay_shutdown = true
    server.net_box:on_shutdown(function()
        while delay_shutdown do
            delay_shutdown_cond:wait()
        end
    end)
end

-- Test changing iproto listen after iproto shutdown is started.
g.test_iproto_listen_after_shutdown_started = function(cg)
    delay_server_connection_shutdown(cg.server)
    cg.server:exec(function()
        local fiber = require('fiber')
        local net = require('net.box')
        local fio = require('fio')
        box.ctl.set_on_shutdown_timeout(10000)
        box.ctl.on_shutdown(_G.delay_shutdown)
        fiber.new(function()
            os.exit()
        end)
        _G.wait_iproto_shutdown()
        local path = fio.pathjoin(fio.cwd(), 'another.sock')
        box.cfg{listen = path}
        -- Server will listen on given URL but will not accept new connection
        -- and will not send greeting etc.
        local conn = net.connect(box.cfg.listen, {connect_timeout = 3})
        t.assert_equals(conn.state, 'error')
        t.assert_str_contains(conn.error, 'timed out')
        conn:close()
    end)
end

-- Test creating new session after iproto shutdown is started.
g.test_box_session_new_after_shutdown_started = function(cg)
    delay_server_connection_shutdown(cg.server)
    cg.server:exec(function()
        local fiber = require('fiber')
        local net = require('net.box')
        local fio = require('fio')
        local socket = require('socket')
        box.ctl.set_on_shutdown_timeout(10000)
        box.ctl.on_shutdown(_G.delay_shutdown)
        fiber.new(function()
            os.exit()
        end)
        _G.wait_iproto_shutdown()
        local path = fio.pathjoin(fio.cwd(), 'some.sock')
        local session_ret, session_err
        local function handler(sock)
            session_ret, session_err = pcall(box.session.new, {fd = sock:fd()})
            if session_ret then
                sock:detach()
            else
                sock:close()
            end
        end
        local server = socket.tcp_server('unix/', path, handler)
        local conn = net.connect(path)
        t.assert_equals(conn.state, 'error')
        t.assert_str_contains(conn.error,
                              'unexpected EOF when reading from socket')
        t.assert_equals(session_ret, false)
        t.assert_str_contains(session_err:unpack().type, 'ClientError')
        t.assert_str_contains(session_err:unpack().message,
                              'Server is shutting down')
        conn:close()
        server:close()
    end)
end
