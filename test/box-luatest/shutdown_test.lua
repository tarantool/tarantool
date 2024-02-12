local server = require('luatest.server')
local utils = require('luatest.utils')
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
    t.assert(cg.server:grep_log('cannot gracefully shutdown iproto', nil,
             {filename = log}))
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
