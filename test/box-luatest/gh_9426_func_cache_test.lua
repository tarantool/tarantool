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

g.test_func_cache = function(cg)
    cg.server:exec(function()
        -- See the explanation in the #9426.
        box.schema.func.create('tortoise')
        box.schema.func.create('dog')
        box.schema.func.drop('tortoise')
        box.schema.func.create('mouse')
        box.schema.func.create('chicken')
        box.schema.func.create('horse')
        box.schema.func.create('rat')
        box.schema.func.drop('horse')
        box.schema.func.create('horse')
        t.assert_equals(box.schema.func.exists('horse'), true)
    end)
end
