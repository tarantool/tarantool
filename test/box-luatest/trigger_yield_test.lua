local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'trigger_yield'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_trigger_yield = function()
    local n_tuples = g.server:exec(function()
        local fiber = require('fiber')

        local function fail()
            fiber.sleep(0.0001)
            error('fail')
        end

        local function insert()
            box.space.test:auto_increment{fiber.id()}
        end

        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.space.test:on_replace(fail)

        local fibers = {}
        for _ = 1, 100 do
            table.insert(fibers, fiber.create(insert))
        end

        for _,f in pairs(fibers) do
            while f:status() ~= 'dead' do
                fiber.sleep(0.0001)
            end
        end

        return box.space.test:len()
    end)
    t.assert_equals(n_tuples, 0)
end
