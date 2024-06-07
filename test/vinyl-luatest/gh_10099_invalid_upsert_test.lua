local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_invalid_upsert = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('pk', {parts = {{1, 'integer'}}})
        s:insert({1})
        s:insert({2})
        s:insert({3})
        box.snapshot()
        box.begin()
        s:upsert({1}, {{'#', 1, 1}})
        s:upsert({2}, {{'=', 1, 's'}})
        s:upsert({3}, {{'=', 1, 1}})
        s:upsert({1}, {{'!', 2, 10}})
        s:upsert({2}, {{'!', 2, 20}})
        s:upsert({3}, {{'!', 2, 30}})
        box.commit()
        t.assert_equals(s:select({}, {fullscan = true}),
                        {{1, 10}, {2, 20}, {3, 30}})
    end)
end
