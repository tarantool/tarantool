local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        box.begin()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        for i = 1, 10 do
            s:replace{i, i, string.rep('a', 100)}
        end
        for i = 11, 11000 do
            s:replace{i, i}
        end
        box.commit()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
    cg.server = nil
end)

-- The test checks that old tuples are unreferenced on background alter
-- of primary index.
g.test_alter_primary_index_tuple_leak = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local finished = false
        local alterer = fiber.create(function()
            box.space.test.index.pk:alter({parts = {2}})
            finished = true
        end)
        alterer:set_joinable(true)

        local items_used = box.slab.info().items_used

        local fibers = {}
        for i = 1, 10 do
            table.insert(fibers, fiber.create(function()
                box.space.test:replace{i, i}
            end))
            fibers[i]:set_joinable(true)
            fiber.yield()
        end
        t.assert_not(finished)
        for i = 1, #fibers do
            t.assert_equals({fibers[i]:join()}, {true})
        end
        t.assert_equals({alterer:join()}, {true})
        collectgarbage('collect')

        -- After all, amount of occupied memory should be lower.
        t.assert_lt(box.slab.info().items_used, items_used)
    end)
end
