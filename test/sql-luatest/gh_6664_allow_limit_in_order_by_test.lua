local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'limit'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a INT, b INT);]])
        box.execute([[INSERT INTO t VALUES(1, 1, 1), (2, 1, 2);]])
        box.execute([[INSERT INTO t VALUES(3, 2, 1), (4, 2, 2);]])
        box.execute([[INSERT INTO t VALUES(5, 3, 1), (6, 3, 2);]])
    end)
end)

g.after_all(function()
    g.server:exec(function()
        box.execute([[DROP TABLE t;]])
    end)
    g.server:stop()
end)

g.test_limit_1 = function()
    g.server:exec(function()
        local sql = [[SELECT * FROM t ORDER BY a ASC, b DESC LIMIT 3;]]
        local res = {{2, 1, 2}, {1, 1, 1}, {4, 2, 2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_limit_2 = function()
    g.server:exec(function()
        local sql = [[SELECT * FROM t ORDER BY a DESC, b ASC LIMIT 3;]]
        local res = {{5, 3, 1}, {6, 3, 2}, {3, 2, 1}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_limit_3 = function()
    g.server:exec(function()
        local sql = [[SELECT * FROM t ORDER BY i DESC, a ASC LIMIT 3;]]
        local res = {{6, 3, 2}, {5, 3, 1}, {4, 2, 2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_limit_4 = function()
    g.server:exec(function()
        local sql = [[SELECT * FROM t ORDER BY a ASC, i DESC, b ASC LIMIT 3;]]
        local res = {{2, 1, 2}, {1, 1, 1}, {4, 2, 2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_limit_5 = function()
    g.server:exec(function()
        local sql = [[SELECT * FROM t ORDER BY a ASC, b DESC LIMIT 3 OFFSET 2;]]
        local res = {{4, 2, 2}, {3, 2, 1}, {6, 3, 2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_limit_6 = function()
    g.server:exec(function()
        local sql = [[SELECT * FROM t ORDER BY a DESC, b ASC LIMIT 3 OFFSET 3;]]
        local res = {{4, 2, 2}, {1, 1, 1}, {2, 1, 2}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_limit_7 = function()
    g.server:exec(function()
        local sql = [[SELECT * FROM t ORDER BY i DESC, a ASC LIMIT 3 OFFSET 4;]]
        local res = {{2, 1, 2}, {1, 1, 1}}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end

g.test_limit_8 = function()
    g.server:exec(function()
        local sql =
            [[SELECT * FROM t ORDER BY a ASC, i DESC, b ASC LIMIT 3 OFFSET 7;]]
        local res = {}
        t.assert_equals(box.execute(sql).rows, res)
    end)
end
