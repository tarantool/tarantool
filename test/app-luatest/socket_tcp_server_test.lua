local t = require('luatest')
local fiber = require('fiber')
local fio = require('fio')
local socket = require('socket')

local g = t.group('socket_tcp_server')

local function wait_fiber_dead(f)
    local deadline = fiber.clock() + 1
    while f:status() ~= 'dead' and fiber.clock() < deadline do
        fiber.sleep(0.1)
    end
    t.assert_equals(f:status(), 'dead')
end

local function wait_fiber_named(prefix)
    local deadline = fiber.clock() + 1
    while fiber.clock() < deadline do
        for _, info in pairs(fiber.info()) do
            local name = info.name or ''
            if name:match('^' .. prefix) ~= nil then
                return true
            end
        end
        fiber.sleep(0.1)
    end
    return false
end

g.before_each(function(cg)
    cg.tmpdir = fio.tempdir()
    cg.sock_path = fio.pathjoin(cg.tmpdir, 'tcp_server.sock')
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        pcall(cg.server.close, cg.server)
    end
    if cg.tmpdir ~= nil then
        fio.rmtree(cg.tmpdir)
    end
end)

g.test_create_and_loop = function(cg)
    local handler = function(sc)
        sc:read(2)
        sc:write('ok')
    end
    local prepare = function(s)
        s:setsockopt(
            'SOL_SOCKET',
            'SO_REUSEADDR',
            true
        )
        return 128
    end
    cg.server, cg.addr = socket.tcp_server_create(
        'unix/',
        cg.sock_path,
        prepare
    )
    t.assert_is_not(cg.server, nil)
    cg.loop_fiber = fiber.create(
        socket.tcp_server_loop,
        cg.server,
        handler
    )
    t.assert(wait_fiber_named('server/'))
    local client = socket.tcp_connect(
        'unix/',
        cg.sock_path
    )
    t.assert_is_not(client, nil)
    client:write('hi')
    t.assert_equals(client:read(2), 'ok')
    client:close()
    cg.server:close()
    cg.server = nil
    wait_fiber_dead(cg.loop_fiber)
end

g.test_stop_on_close = function(cg)
    local handler = function(sc)
        sc:read(1)
    end
    cg.server = socket.tcp_server_create(
        'unix/',
        cg.sock_path,
        {handler = handler}
    )
    t.assert_is_not(cg.server, nil)
    cg.loop_fiber = fiber.create(
        socket.tcp_server_loop,
        cg.server,
        {handler = handler, name = 'optsserv'}
    )
    t.assert(wait_fiber_named('optsserv/'))
    cg.server:close()
    cg.server = nil
    wait_fiber_dead(cg.loop_fiber)
end

g.test_create_with_opts_table = function(cg)
    local handler = function(sc)
        sc:write('hi')
    end
    cg.server = socket.tcp_server_create(
        'unix/',
        cg.sock_path,
        {
            prepare = function() return 1 end
        }
    )
    t.assert_is_not(cg.server, nil)
    cg.loop_fiber = fiber.create(
        socket.tcp_server_loop,
        cg.server:fd(),
        {
            handler = handler,
            name = 'optsserv',
        }
    )
    local client = socket.tcp_connect(
        'unix/',
        cg.sock_path
    )
    t.assert_is_not(client, nil)
    t.assert_equals(client:read(2), 'hi')
    client:close()
    cg.server:close()
    cg.server = nil
    wait_fiber_dead(cg.loop_fiber)
end
