local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function()
    g.server = server:new({alias = 'box_runtime_info_memory'})
    g.server:start()
end)

g.after_each(function()
    g.server:drop()
end)

g.test_basic = function()
    g.server:exec(function()
        -- Keep references to tuples to ensure that they're not
        -- suddenly garbage collected.
        local tuples = {}

        -- The statistics increased when new runtime tuples are
        -- allocated.
        local memory_a = box.runtime.info().tuple
        for i = 1, 100 do
            table.insert(tuples, box.tuple.new({i}))
        end
        local memory_b = box.runtime.info().tuple
        t.assert_gt(memory_b, memory_a)

        -- The statistics decreased when runtime tuples are
        -- collected as garbage.
        tuples = nil -- luacheck: no unused
        collectgarbage()
        local memory_c = box.runtime.info().tuple
        t.assert_lt(memory_c, memory_b)
    end)
end
