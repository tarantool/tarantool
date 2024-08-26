local server = require('luatest.server')
local utils = require('luatest.utils')
local justrun = require('luatest.justrun')
local fio = require('fio')
local popen = require('popen')
local fiber = require('fiber')
local t = require('luatest')

-- Luatest server currently does not allow to check process exit code.
local g_crash = t.group('crash')

g_crash.before_each(function(cg)
    local id = ('%s-%s'):format('server', utils.generate_id())
    cg.workdir = fio.pathjoin(server.vardir, id)
    fio.mkdir(cg.workdir)
end)

g_crash.after_each(function(cg)
    if cg.handle ~= nil then
        cg.handle:close()
    end
    cg.handle = nil
end)

local tarantool = arg[-1]

-- Test there is no assertion on shutdown during snapshot
-- that triggered by SIGUSR1.
g_crash.test_shutdown_during_snapshot_on_signal = function(cg)
    t.tarantool.skip_if_not_debug()
    local script = [[
        local fio = require('fio')
        local log = require('log')

        local workdir = os.getenv('TARANTOOL_WORKDIR')
        fio.chdir(workdir)
        local logfile = fio.pathjoin(workdir, 'server.log')
        box.cfg{log = logfile}
        box.schema.create_space('test')
        box.space.test:create_index('pk')
        box.begin()
        for i=1,10000 do
            box.space.test:insert{i}
        end
        box.commit()
        box.error.injection.set('ERRINJ_SNAP_WRITE_TIMEOUT', 0.01)
        log.info('server startup finished')
    ]]
    -- Inherit environment to inherit ASAN suppressions.
    local env = table.copy(os.environ())
    env.TARANTOOL_WORKDIR = cg.workdir
    local handle, err = popen.new({tarantool, '-e', script},
                                   {stdin = popen.opts.DEVNULL,
                                    stdout = popen.opts.DEVNULL,
                                    stderr = popen.opts.DEVNULL,
                                    env = env})
    assert(handle, err)
    cg.handle = handle
    local logfile = fio.pathjoin(cg.workdir, 'server.log')
    t.helpers.retrying({}, function()
        t.assert(server.grep_log(nil, 'server startup finished', nil,
                                 {filename = logfile}))
    end)
    -- To drop first 'saving snapshot' entry.
    assert(fio.truncate(logfile))
    -- Start snapshot using signal.
    assert(handle:signal(popen.signal.SIGUSR1))
    t.helpers.retrying({}, function()
        t.assert(server.grep_log(nil, 'saving snapshot', nil,
                                 {filename = logfile}))
    end)
    assert(handle:signal(popen.signal.SIGTERM))
    local status = handle:wait()
    t.assert_equals(status.state, 'exited')
    t.assert_equals(status.exit_code, 0)
end

-- Test shutdown when new iproto connection is accepted but
-- not yet fully established.
g_crash.test_shutdown_during_new_connection = function(cg)
    local script = [[
        local net = require('net.box')
        local fiber = require('fiber')

        local sock = 'unix/:./iproto.sock'
        box.cfg{listen = sock}
        local cond = fiber.cond()
        local in_trigger = false
        box.session.on_connect(function()
            in_trigger = true
            cond:signal()
            fiber.sleep(1)
        end)
        net.connect(sock, {wait_connected = false})
        while not in_trigger do
            cond:wait()
        end
        os.exit()
    ]]
    local result = justrun.tarantool(cg.workdir, {}, {'-e', script},
                                     {nojson = true, quote_args = true})
    t.assert_equals(result.exit_code, 0)
end

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

local function test_no_hang_on_shutdown(server)
    local channel = fiber.channel()
    fiber.create(function()
        server:stop()
        channel:put('finished')
    end)
    t.assert(channel:get(60) ~= nil)
end

-- Test shutdown does not hang if there is iproto request that
-- cannot be cancelled.
g.test_shutdown_of_hanging_iproto_request = function(cg)
    fiber.new(function()
        cg.server:exec(function()
            local log = require('log')
            local fiber = require('fiber')
            local tweaks = require('internal.tweaks')
            tweaks.box_shutdown_timeout = 1.0
            log.info('going to sleep for test')
            while true do
                pcall(fiber.sleep, 1000)
            end
        end)
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('going to sleep for test'))
    end)
    local log = fio.pathjoin(cg.server.workdir, cg.server.alias .. '.log')
    test_no_hang_on_shutdown(cg.server)
    t.assert(cg.server:grep_log('cannot gracefully shutdown iproto requests',
             nil, {filename = log}))
end

-- Test shutdown does not hang if there is client fiber that
-- cannot be cancelled.
g.test_shutdown_of_hanging_client_fiber = function(cg)
    cg.server:exec(function()
        local log = require('log')
        local fiber = require('fiber')
        local tweaks = require('internal.tweaks')
        tweaks.box_shutdown_timeout = 1.0
        fiber.new(function()
            log.info('going to sleep for test')
            while true do
                pcall(fiber.sleep, 1000)
            end
        end)
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('going to sleep for test'))
    end)
    local log = fio.pathjoin(cg.server.workdir, cg.server.alias .. '.log')
    test_no_hang_on_shutdown(cg.server)
    t.assert(cg.server:grep_log('cannot gracefully shutdown client fibers', nil,
             {filename = log}))
end

-- Let's test we free tuples on index drop and immediate shutdown, before
-- memtx gc fiber have a chance to free all tuples. That is we take care
-- too free tuples during shutdown. The actual test is done by LSAN.
-- This way we make sure other tests in similar situation are not flaky
-- under ASAN.
g.test_shutdown_memtx_gc_free_tuples = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        box.schema.create_space('test')
        box.space.test:create_index('pk')
        box.space.test:insert{1}
        box.error.injection.set('ERRINJ_MEMTX_DELAY_GC', true)
        box.space.test.index.pk:drop()
    end)
    server:stop()
end

g.test_shutdown_with_active_connection = function(cg)
    -- Define helpers needed for the test
    local function socket_connect(server)
        local lsocket = require('socket')
        local uri = require('uri')
        local u = uri.parse(server.net_box_uri)
        local s = lsocket.tcp_connect(u.host, u.service)
        t.assert_not_equals(s, nil)
        -- Skip the greeting.
        s:read(box.iproto.GREETING_SIZE, 10)
        return s
    end
    local function write_eval(s, expr)
        local key = box.iproto.key
        local header = {
            [key.REQUEST_TYPE] = box.iproto.type.EVAL,
            [key.SYNC] = 1,
        }
        local body = {
            [key.EXPR] = expr,
            [key.TUPLE] = {},
        }
        return s:write(box.iproto.encode_packet(header, body))
    end

    -- Delay shutdown for ~5 seconds
    cg.server:exec(function()
        local log = require('log')
        local fiber = require('fiber')
        local tweaks = require('internal.tweaks')
        tweaks.box_shutdown_timeout = 30.0
        fiber.new(function()
            log.info('going to sleep for test')
            local i = 0
            while i < 5 do
                pcall(fiber.sleep, 1)
                i = i + 1
            end
        end)
    end)

    -- Start a fiber that will log a message when all client fibers
    -- will be cancelled
    cg.server:exec(function()
        local log = require('log')
        local fiber = require('fiber')
        fiber.new(function()
            pcall(fiber.sleep, 100)
            log.info('Client fibers were cancelled')
        end)
    end)

    -- Connect socket to Tarantool before shutting down
    -- Net box is not suitable because it supports graceful shutdown
    -- so we can't send a new request with it when Tarantool is shutting
    -- down
    local socket = socket_connect(cg.server)

    -- Start stopping server
    local f = fiber.create(function()
        cg.server:stop()
    end)
    f:set_joinable(true)

    -- Wait until Tarantool will cancel client fibers
    local log = fio.pathjoin(cg.server.workdir, cg.server.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('Client fibers were cancelled', nil,
            {filename = log}))
    end)

    -- Send a request that could make Tarantool panic on shutdown
    -- if it was accepted
    local expr = "require('fiber').sleep(1000)"
    write_eval(socket, expr)

    -- Check if server is successfully stopped
    local ok, err = f:join()
    t.assert(ok, err)
end

local g_idle_pool = t.group('idle pool')

g_idle_pool.before_each(function(cg)
    cg.server = server:new({
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG = [[
                local tweaks = require('internal.tweaks')
                tweaks.box_fiber_pool_idle_timeout = 100
            ]]
        }
    })
    cg.server:start()
end)

g_idle_pool.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

-- Test shutdown with idle fiber in fiber pool.
g_idle_pool.test_shutdown_fiber_pool_with_idle_fibers = function(cg)
    t.tarantool.skip_if_not_debug()
    -- Just to create idle fiber in pool after request.
    cg.server:exec(function() end)
    test_no_hang_on_shutdown(cg.server)
end
