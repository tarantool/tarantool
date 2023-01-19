local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{
        alias = 'default',
    }
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        box.schema.space.create('test', { if_not_exists = true })
        box.space.test:format({
            {name='id',type='integer'},
            {name='t1', type='string'}
        })
        box.space.test:create_index('pri', { parts = {'id'} })
        box.space.test:create_index('i', { parts = {'t1'}, hint = false })
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

g.test_qsort_recovery = function()
    g.server:exec(function()

        local PACK = 10000
        local TOTAL = 2000000
        local uuid = require "uuid"
        local fiber = require "fiber"

        for i = 1, TOTAL / PACK do
            if fiber.set_slice then
                fiber.set_slice(600)
            end
            box.begin()
            for k = 1, PACK do
                box.space.test:replace{(i - 1) * 1000 + k, uuid.str()}
            end
            box.commit()
        end

        box.snapshot()

    end)

    g.server:restart()

    -- check secondary index correctness
    g.server:exec(function()
        local PACK = 10000
        local t = require('luatest')
        local prev_tuple = nil
        local fiber = require "fiber"
        local i = 0
        for _,tuple in box.space.test.index.i:pairs() do
            if prev_tuple ~= nil then
                t.assert(prev_tuple[2] < tuple[2], "Unordered!")
            end
            prev_tuple = tuple
            i = i + 1
            if i == PACK then
                fiber.yield()
                i = 0
            end
        end
    end)

    -- original test
    g.server:exec(function()

        local PACK = 10000
        local TOTAL = 2000000
        local uuid = require "uuid"
        local fiber = require "fiber"

        for i = 1, TOTAL / PACK do
            if fiber.set_slice then
                fiber.set_slice(600)
            end
            box.begin()
            for k = 1, PACK do
                box.space.test:replace{(i - 1) * 1000 + k, uuid.str()}
            end
            box.commit()
        end

    end)

end
