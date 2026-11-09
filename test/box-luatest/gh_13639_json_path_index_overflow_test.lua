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

-- A JSON path index out of the int range must be rejected instead of
-- wrapping around to a valid field number.
g.test_update_index_overflow = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:insert({0})
        t.assert_error(s.update, s, {0}, {{'=', '[4294967298]', 1}})
        t.assert_equals(s:get({0}):totable(), {0})
    end)
end
