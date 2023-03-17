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

g.test_wrong_big_uint_to_dec_convertion = function()
    g.server:exec(function()
        local ret = box.execute([[SELECT 1.0 + 18446744073709551615;]])
        local res = require('decimal').new('18446744073709551616')
        t.assert_equals(ret.rows[1][1], res)
    end)
end

g.test_wrong_neg_int_to_dec_convertion = function()
    g.server:exec(function()
        local ret = box.execute([[SELECT 1.0 + -1;]])
        t.assert_equals(ret.rows[1][1], require('decimal').new(0))
    end)
end
