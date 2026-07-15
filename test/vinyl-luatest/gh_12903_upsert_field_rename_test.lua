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

g.test_upsert_survives_field_rename = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:format({{'a', 'unsigned'}, {'b', 'unsigned'}})
        s:create_index('primary')
        for i = 2, 100 do
            s:insert({i, i})
        end
        box.snapshot()
        s:upsert({1, 10}, {{'=', 'b', 10}})
        s:upsert({2, 20}, {{'=', 'b', 20}})
        box.snapshot()

        s:format({{'x', 'unsigned'}, {'y', 'unsigned'}})

        t.assert_equals(s:select({}, {limit = 2}), {{1, 10}, {2, 20}})
    end)
end
