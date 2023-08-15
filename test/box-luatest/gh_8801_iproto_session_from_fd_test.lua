local fio = require('fio')
local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

local SOCK_PATH = fio.pathjoin(server.vardir, 'gh-8801.sock')

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {iproto_threads = 4}})
    cg.server:start()

    -- Start a TCP server listening on SOCK_PATH.
    --
    -- The server handler will accept all incoming connections with
    -- box.session.new(opts).
    cg.start_listen = function(opts)
        cg.server:exec(function(sock_path, opts)
            local socket = require('socket')
            t.assert_not(rawget(_G, 'listen_sock'))
            local function handler(sock)
                local opts2 = opts and table.copy(opts) or {}
                opts2.fd = sock:fd()
                box.session.new(opts2)
                sock:detach()
            end
            local listen_sock = socket.tcp_server('unix/', sock_path, handler)
            t.assert(listen_sock)
            rawset(_G, 'listen_sock', listen_sock)
        end, {SOCK_PATH, opts})
    end

    -- Stop the server started with start_listen.
    cg.stop_listen = function()
        cg.server:exec(function()
            local listen_sock = rawget(_G, 'listen_sock')
            if listen_sock then
                listen_sock:close()
                rawset(_G, 'listen_sock', nil)
            end
        end)
    end
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.stop_listen()
end)

-- Checks that box.cfg() must be called.
g.test_no_cfg = function()
    t.assert_error_msg_equals("Please call box.cfg{} first",
                              box.session.new, {fd = 0})
end

-- Checks errors raised on invalid arguments.
g.test_invalid_args = function(cg)
    cg.server:exec(function()
        t.assert_error_msg_equals(
            "Illegal parameters, options should be a table",
            box.session.new, 'foo')
        t.assert_error_msg_equals(
            "Illegal parameters, unexpected option 'foo'",
            box.session.new, {foo = 'bar'})
        t.assert_error_msg_equals(
            "Illegal parameters, " ..
            "options parameter 'type' should be of type string",
            box.session.new, {type = 0})
        t.assert_error_msg_equals(
            "Illegal parameters, invalid session type 'foo', " ..
            "the only supported type is 'binary'",
            box.session.new, {type = 'foo'})
        t.assert_error_msg_equals(
            "Illegal parameters, options parameter 'fd' is mandatory",
            box.session.new, {})
        t.assert_error_msg_equals(
            "Illegal parameters, " ..
            "options parameter 'fd' should be of type number",
            box.session.new, {fd = 'foo'})
        t.assert_error_msg_equals(
            "Illegal parameters, " ..
            "options parameter 'fd' must be nonnegative integer",
            box.session.new, {fd = -1})
        t.assert_error_msg_equals(
            "Illegal parameters, " ..
            "options parameter 'fd' must be nonnegative integer",
            box.session.new, {fd = 2 ^ 31})
        t.assert_error_msg_equals(
            "Illegal parameters, " ..
            "options parameter 'fd' must be nonnegative integer",
            box.session.new, {fd = 1.5})
        t.assert_error_msg_equals(
            "Illegal parameters, " ..
            "options parameter 'user' should be of type string",
            box.session.new, {user = 0})
        t.assert_error_msg_equals(
            "User 'foo' is not found",
            box.session.new, {fd = 0, user = 'foo'})
        t.assert_error_msg_equals(
            "Illegal parameters, " ..
            "options parameter 'storage' should be of type table",
            box.session.new, {storage = 'foo'})
    end)
end

-- Checks default options.
g.test_defaults = function(cg)
    cg.start_listen()
    local conn = net.connect(SOCK_PATH)
    t.assert(conn.state, 'active')
    t.assert_equals(conn:call('box.session.type'), 'binary')
    t.assert_equals(conn:call('box.session.peer'), 'unix/:(socket)')
    t.assert_equals(conn:call('box.session.user'), 'guest')
    t.assert_equals(conn:eval('return box.session.storage'), {})
    conn:close()
end

-- Checks that one may specify a custom session user.
g.test_custom_user = function(cg)
    cg.start_listen({user = 'admin'})
    local conn = net.connect(SOCK_PATH)
    t.assert(conn.state, 'active')
    t.assert_equals(conn:call('box.session.type'), 'binary')
    t.assert_equals(conn:call('box.session.peer'), 'unix/:(socket)')
    t.assert_equals(conn:call('box.session.user'), 'admin')
    t.assert_equals(conn:eval('return box.session.storage'), {})
    conn:close()
end

-- Checks that one may override a custom session user by passing credentials.
g.test_custom_user_override = function(cg)
    cg.start_listen({user = 'admin'})
    local conn = net.connect(SOCK_PATH, {user = 'guest'})
    t.assert(conn.state, 'active')
    t.assert_equals(conn:call('box.session.type'), 'binary')
    t.assert_equals(conn:call('box.session.peer'), 'unix/:(socket)')
    t.assert_equals(conn:call('box.session.user'), 'guest')
    t.assert_equals(conn:eval('return box.session.storage'), {})
    conn:close()
end

-- Checks that one may specify a custom session storage.
g.test_custom_storage = function(cg)
    local storage = {foo = 1, bar = 2}
    cg.start_listen({storage = storage})
    local conn = net.connect(SOCK_PATH)
    t.assert(conn.state, 'active')
    t.assert_equals(conn:call('box.session.type'), 'binary')
    t.assert_equals(conn:call('box.session.peer'), 'unix/:(socket)')
    t.assert_equals(conn:call('box.session.user'), 'guest')
    t.assert_equals(conn:eval('return box.session.storage'), storage)
    conn:close()
end

-- Checks that one may specify the 'binary' session type explicitly.
g.test_session_type = function(cg)
    cg.start_listen({type = 'binary'})
    local conn = net.connect(SOCK_PATH)
    t.assert(conn.state, 'active')
    t.assert_equals(conn:call('box.session.type'), 'binary')
    t.assert_equals(conn:call('box.session.peer'), 'unix/:(socket)')
    t.assert_equals(conn:call('box.session.user'), 'guest')
    t.assert_equals(conn:eval('return box.session.storage'), {})
    conn:close()
end

-- Checks that connections are distributed evenly among all threads.
g.test_threads = function(cg)
    cg.start_listen()
    local COUNT = 20
    local conns = {}
    for i = 1, COUNT do
        conns[i] = net.connect(SOCK_PATH)
        t.assert(conns[i].state, 'active')
    end
    cg.server:exec(function(COUNT)
        t.assert(box.cfg.iproto_threads > 1)
        for i = 1, box.cfg.iproto_threads do
            t.assert_ge(box.stat.net.thread[i].CONNECTIONS.current,
                        COUNT / box.cfg.iproto_threads)
        end
    end, {COUNT})
    for i = 1, COUNT do
        conns[i]:close()
    end
end

g.after_test('test_invalid_fd', function(cg)
    cg.server:exec(function()
        for _, func in ipairs(box.session.on_connect()) do
            box.session.on_connect(nil, func)
        end
        t.assert_equals(box.session.on_connect(), {})
    end)
end)

-- Checks that the session created from an invalid fd is closed.
g.test_invalid_fd = function(cg)
    cg.server:exec(function()
        local sid, fd, peer
        box.session.on_connect(function()
            sid = box.session.id()
            fd = box.session.fd()
            peer = box.session.peer()
        end)
        box.session.new({fd = 9000})
        t.helpers.retrying({}, function()
            t.assert_is_not(sid, nil)
        end)
        t.assert_equals(fd, 9000)
        t.assert_is(peer, nil)
        t.helpers.retrying({}, function()
            t.assert_not(box.session.exists(sid))
        end)
    end)
end
