local server = require('luatest.server')
local fio = require('fio')
local fiber = require('fiber')
local t = require('luatest')

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
