local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(g)
    g.server = server:new()
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.test_function_create_first = function(g)
    g.server:exec(function()
        for _, v in box.space._func:pairs() do
            box.schema.func.drop(v.name)
        end
        box.schema.func.create('test1')
        box.schema.func.create('test2')
        local test1 = box.space._vfunc.index.name:get('test1')
        local test2 = box.space._vfunc.index.name:get('test2')
        t.assert_equals(test1.id, 1)
        t.assert_equals(test2.id, 2)
    end)
end
