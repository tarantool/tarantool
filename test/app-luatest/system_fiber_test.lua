local server = require('luatest.server')
local g = require('luatest').group()

g.before_all(function()
    g.server = server:new{alias = 'default'}
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.test_system_fibers = function()
    g.server:exec(function(uri)
        local fiber = require('fiber')
        local t = require('luatest')

        local conn = require('net.box').connect(uri)
        local is_system = function(name)
            return name:endswith('(net.box)') or
                   name == 'gc' or name == 'checkpoint_daemon' or
                   name:startswith('vinyl.')
        end

        local system_fibers = {}
        for fid, f in pairs(fiber.info()) do
            if is_system(f.name) then
                -- fiber.find() + fiber:cancel() == fiber.kill()
                local fiber_temp = fiber.find(fid)
                table.insert(system_fibers, fiber_temp)
                fiber_temp:cancel()
            end
        end

        fiber.yield()
        t.assert(conn:ping())
        for _, f in pairs(system_fibers) do
            t.assert(f:status() ~= 'dead')
        end
    end, {g.server.net_box_uri})
end
