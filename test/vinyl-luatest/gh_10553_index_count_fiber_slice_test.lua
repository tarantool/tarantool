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
        box.snapshot()
    end)
end)

g.test_index_count_fiber_slice = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk')
        s:create_index('sk', {unique = false, parts = {2, 'unsigned'}})

        box.begin()
        for i = 1, 100 * 1000 do
            s:replace({i, i * 2})
            if i % 1000 == 0 then
                box.commit()
                box.begin()
            end
        end
        box.commit()

        fiber.self():set_max_slice(0.01)
        t.assert_error_covers({type = 'FiberSliceIsExceeded'},
                              s.index.sk.count, s.index.sk)
    end)
end
