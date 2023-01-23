local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'bind'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_bind_1 = function()
    g.server:exec(function()
        local sql = [[SELECT @1asd;]]
        local res = "At line 1 at or near position 9: unrecognized token '1asd'"
        local _, err = box.execute(sql, {{['@1asd'] = 123}})
        t.assert_equals(err.message, res)
    end)
end

g.test_bind_2 = function()
    local conn = g.server.net_box
    local sql = [[SELECT @1asd;]]
    local res = [[At line 1 at or near position 9: unrecognized token '1asd']]
    local _, err = pcall(conn.execute, conn, sql, {{['@1asd'] = 123}})
    t.assert_equals(err.message, res)
end
