local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        box.schema.create_space('test', {engine = 'vinyl'})
        box.space.test:create_index('primary', {
            parts = {1, 'unsigned'},
        })
        box.space.test:create_index('secondary', {
            parts = {2, 'string'}, unique = false,
        })
        for i = 1, 10 do
            box.space.test:insert({i, string.rep('x', 1000)})
            if i == 5 then
                box.snapshot()
            end
        end
        collectgarbage()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_tuple_stat = function(cg)
    cg.server:exec(function()
        local function gc()
            box.tuple.new() -- drop blessed tuple ref
            collectgarbage()
        end
        -- Initial value is 0.
        gc()
        t.assert_equals(box.stat.vinyl().memory.tuple, 0)

        -- Tuples pinned by Lua.
        box.cfg({vinyl_cache = 0})
        local ret = box.space.test:select()
        t.assert_equals(#ret, 10)
        t.assert_almost_equals(box.stat.vinyl().memory.tuple, 10 * 1000, 1000)
        ret = nil -- luacheck: ignore
        gc()
        t.assert_equals(box.stat.vinyl().memory.tuple, 0)

        -- Tuples pinned by cache.
        box.cfg({vinyl_cache = 100 * 1000})
        ret = box.space.test:select()
        t.assert_equals(#ret, 10)
        t.assert_almost_equals(box.stat.vinyl().memory.tuple, 10 * 1000, 1000)
        ret = nil -- luacheck: ignore
        gc()
        t.assert_almost_equals(box.stat.vinyl().memory.tuple, 10 * 1000, 1000)
        box.cfg({vinyl_cache = 0})
        t.assert_equals(box.stat.vinyl().memory.tuple, 0)
    end)
end
