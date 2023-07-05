local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_exists = function()
    g.server:exec(function()
        local res = box.execute([[SELECT EXISTS (VALUES (1), (2));]])
        t.assert_equals(res.rows, {{true}})
    end)
end
