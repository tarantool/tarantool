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

g.test_tuple_hash_double = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk', {
            type = 'hash',
            parts = {
                {1, 'double'}, {2, 'string'}, {3, 'double'},
            },
        })
        s:insert{1, 'x', 0.5}
        s:insert{0.5, 'y', 1}
        s:delete{0.5, 'y', 1}
        t.assert_equals(s:select({}, {fullscan = true}), {{1, 'x', 0.5}})
    end)
end
