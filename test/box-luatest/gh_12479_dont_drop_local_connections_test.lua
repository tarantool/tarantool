local t = require('luatest')
local server = require('luatest.server')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.test_local_connections_survive = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'test.lua', [[
        local fiber = require('fiber')
        local net = require('net.box')
        local socket = require('socket')

        box.cfg{log_level = 'warn'}

        local s1, s2 = socket.socketpair('AF_UNIX', 'SOCK_STREAM', 0)
        local fd1, fd2 = s1:fd(), s2:fd()
        s1:detach()
        s2:detach()
        box.session.new({fd = fd1, user = 'admin'})
        local conn = net.from_fd(fd2)

        local chan = fiber.channel()
        rawset(_G, 'chan', chan)

        local future = conn:eval([=[
            local fiber = require('fiber')
            chan:put(1)
            chan:get()
            return true
        ]=], {}, {is_async = true})

        chan:get()
        -- Check dropping is successful.
        box.iproto.internal.drop_connections(30)
        assert(conn.state == 'active', 'connection state: ' .. conn.state)
        -- Check long polling request is not cancelled.
        assert(chan:put(2, 30), 'put error')
        assert(future:wait_result())

        os.exit()
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, {'test.lua'}, opts)
    t.assert_equals(res.exit_code, 0, {res.stdout, res.stderr})
end

g.after_test('test_shutdown_local_connection', function(cg)
    -- Test there is no hang on shutdown indirectly.
    cg.server:drop()
end)

-- Just cover case of shutdown with local connection.
g.test_shutdown_local_connection = function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local net = require('net.box')
        local socket = require('socket')

        local s1, s2 = socket.socketpair('AF_UNIX', 'SOCK_STREAM', 0)
        local fd1, fd2 = s1:fd(), s2:fd()
        s1:detach()
        s2:detach()
        box.session.new({fd = fd1, user = 'admin'})
        local conn = net.from_fd(fd2)
        rawset(_G, 'conn', conn)
    end)
end
