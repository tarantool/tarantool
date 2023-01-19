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

g.test_ghs_80 = function()
    g.server:exec(function()
        local sql = [[select 'a' collate a union select 'b' collate "binary";]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, "Collation 'A' does not exist")
    end)
end
