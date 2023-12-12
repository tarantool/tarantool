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

g.test_too_big_decimal = function()
    g.server:exec(function()
        local dec = '111111111111111111111111111111111111111.0'
        local sql = ([[SELECT %s;]]):format(dec)
        local exp = ([[Invalid decimal: '%s']]):format(dec)
        local res, err = box.execute(sql)
        t.assert(res == nil)
        t.assert_equals(err.message, exp)

        sql = ([[SELECT -%s;]]):format(dec)
        res, err = box.execute(sql)
        t.assert(res == nil)
        t.assert_equals(err.message, exp)
    end)
end
