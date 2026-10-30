local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'gh-6988'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- Make sure that ROUND() with DECIMAL as the first argument works as intended.
g.test_round_dec = function()
    g.server:exec(function()
        local sql = [[SELECT ROUND(1.13154);]]
        local res = {{1}}
        t.assert_equals(box.execute(sql).rows, res)

        sql = [[SELECT ROUND(-1123432.13154);]]
        res = {{-1123432}}
        t.assert_equals(box.execute(sql).rows, res)

        sql = [[SELECT ROUND(9999123432.13154, 3);]]
        res = {{9999123432.132}}
        t.assert_equals(box.execute(sql).rows, res)

        sql = [[SELECT ROUND(-562323432.13154, 10000000);]]
        res = {{-562323432.13154}}
        t.assert_equals(box.execute(sql).rows, res)

        sql = [[SELECT ROUND(-562323432.13154, -10000000);]]
        res = {{-562323432}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure that ROUND() with INTEGER as the first argument works as intended.
g.test_round_int = function()
    g.server:exec(function()
        local sql = [[SELECT ROUND(113154);]]
        local res = {{113154}}
        t.assert_equals(box.execute(sql).rows, res)

        sql = [[SELECT ROUND(-1123432);]]
        res = {{-1123432}}
        t.assert_equals(box.execute(sql).rows, res)

        sql = [[SELECT ROUND(9999123432, 3);]]
        res = {{9999123432}}
        t.assert_equals(box.execute(sql).rows, res)

        sql = [[SELECT ROUND(-562323432, 10000000);]]
        res = {{-562323432}}
        t.assert_equals(box.execute(sql).rows, res)

        sql = [[SELECT ROUND(-562323432, -10000000);]]
        res = {{-562323432}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

-- Make sure that the default type for the first argument of ROUND() is DECIMAL.
g.test_round_default_type = function()
    g.server:exec(function()
        local sql = [[SELECT TYPEOF(ROUND(?));]]
        local res = {{'decimal'}}
        t.assert_equals(box.execute(sql, {1}).rows, res)
    end)
end
