local server = require('luatest.server')
local utils = require('luatest.utils')
local justrun = require('test.justrun')
local fio = require('fio')
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
    t.assert(cg.server:grep_log('cannot gracefully shutdown iproto', nil,
             {filename = log}))
end
