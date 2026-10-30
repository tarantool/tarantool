local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_schema_downgrade_to_2_11_5 = function(cg)
    cg.server:exec(function()
        box.schema.downgrade("2.11.5")
        t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 5})
    end)
end
