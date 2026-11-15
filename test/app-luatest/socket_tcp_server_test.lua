local t = require('luatest')
local treegen = require('luatest.treegen')
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
    cg.cwd = fio.cwd()
    cg.tmpdir = treegen.prepare_directory({}, {})
    cg.sock_path = fio.pathjoin(cg.tmpdir, 'tcp_server.sock')
end)

g.after_each(function(cg)
    fio.chdir(cg.cwd)
    if cg.server ~= nil then
        pcall(cg.server.close, cg.server)
    end
    if cg.second_server ~= nil then
        pcall(cg.second_server.close, cg.second_server)
    end
    if cg.tmpdir then
        fio.rmtree(cg.tmpdir)
    end
end)

-- Verify closing after chdir removes only the original socket file.
g.test_close_after_chdir = function(cg)
    local first = fio.pathjoin(cg.tmpdir, 'first')
    local second = fio.pathjoin(cg.tmpdir, 'second')
    t.assert(fio.mkdir(first))
    t.assert(fio.mkdir(second))
    t.assert(fio.chdir(first))
    cg.server = socket.tcp_server_create('unix/', 'c.sock', {})
    t.assert_not_equals(cg.server, nil)
    t.assert(fio.chdir(second))
    cg.second_server = socket.tcp_server_create('unix/', 'c.sock', {})
    t.assert_not_equals(cg.second_server, nil)
    local inode = fio.stat('c.sock').inode

    cg.server:close()
    cg.server = nil
    t.assert_equals(fio.lstat(fio.pathjoin(first, 'c.sock')), nil)
    t.assert_equals(fio.stat('c.sock').inode, inode)
    local client = socket.tcp_connect('unix/', 'c.sock', 2)
    t.assert_not_equals(client, nil)
    client:close()
end

-- Verify short relative sockets work and clean up in long directory paths.
g.test_close_long_relative_path = function(cg)
    local dir = fio.pathjoin(cg.tmpdir, string.rep('d', 120))
    t.assert(fio.mkdir(dir))
    t.assert(fio.chdir(dir))
    cg.server = socket.tcp_server_create('unix/', 'c.sock', {})
    t.assert_not_equals(cg.server, nil)
    local client = socket.tcp_connect('unix/', 'c.sock', 2)
    t.assert_not_equals(client, nil)
    client:close()

    t.assert(fio.chdir(cg.cwd))
    cg.server:close()
    cg.server = nil
    t.assert_equals(fio.lstat(fio.pathjoin(dir, 'c.sock')), nil)
end

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
