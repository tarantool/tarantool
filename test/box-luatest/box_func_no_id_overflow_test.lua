local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(g)
    g.server = server:new()
    g.server:start()
end)

g.test_function_create_drop_multiple = function(g)
    g.server:exec(function()
        local BOX_FUNCTION_MAX = 32000
        box.schema.func.create("first")
        box.schema.func.create("second")
        for _ = 1, 2 * BOX_FUNCTION_MAX do
            box.schema.func.drop("first")
            box.schema.func.create("first")
            box.schema.func.drop("second")
            box.schema.func.create("second")
        end
    end)
end

g.after_all(function(g)
    g.server:drop()
end)
