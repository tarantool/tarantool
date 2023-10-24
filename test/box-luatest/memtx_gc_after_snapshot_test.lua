local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

-- Checks that memtx tuple garbage collection is resumed after snapshot.
g.test_memtx_gc_after_snapshot = function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
        local mem_used_1 = box.info.memory().data
        box.begin()
        for i = 1, 100 do
            box.space.test:insert({i, string.rep('x', 10000)})
        end
        box.commit()
        local mem_used_2 = box.info.memory().data
        t.assert_gt(mem_used_2 - mem_used_1, 1000000)
        box.snapshot()
        box.begin()
        for i = 1, 100 do
            box.space.test:delete(i)
        end
        box.commit()
        box.tuple.new() -- drop blessed tuple ref
        collectgarbage('collect') -- drop Lua refs
        local mem_used_3 = box.info.memory().data
        t.assert_gt(mem_used_2 - mem_used_3, 1000000)
    end)
end
