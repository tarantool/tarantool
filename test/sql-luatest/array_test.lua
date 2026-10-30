local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'array'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- Make sure that ARRAY values can be used as bound variable.
g.test_array_binding_local = function()
    g.server:exec(function()
        local sql = [[SELECT #a;]]
        local arg = {{['#a'] = {1, 2, 3}}}
        t.assert_equals(box.execute(sql, arg).rows[1][1], {1, 2, 3})
    end)
end

g.test_array_binding_remote = function()
    local conn = g.server.net_box
    local ok, res = pcall(conn.execute, conn, [[SELECT #a;]],
                          {{['#a'] = {1, 2, 3}}})
    t.assert_equals(ok, true)
    t.assert_equals(res.rows[1][1], {1, 2, 3})
end
