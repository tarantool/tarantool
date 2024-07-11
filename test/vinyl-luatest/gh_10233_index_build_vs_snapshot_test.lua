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

g.test_index_build_vs_snapshot = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk')
        s:insert({1, 1})
        local f1 = fiber.new(function()
            s:create_index('sk', {parts = {2, 'unsigned'}})
        end)
        f1:set_joinable(true)
        local f2 = fiber.new(function()
            box.snapshot()
        end)
        f2:set_joinable(true)
        fiber.sleep(0.1)
        local timeout = 5
        t.assert_equals({f1:join(timeout)}, {true})
        t.assert_equals({f2:join(timeout)}, {true})
    end)
end
