local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_round_double'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_round_double = function()
    g.server:exec(function()
        local sql = [[SELECT ROUND(1.2345678901234e0, 2147483647);]]
        t.assert_equals(box.execute(sql).rows, {{1.2345678901234}})
    end)
end
