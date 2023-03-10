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

g.test_select_lead_to_assertion = function()
    g.server:exec(function()
        local res = box.execute([[SELECT * FROM "_space" WHERE "owner" = 1;]])
        t.assert(res ~= nil)
    end)
end
