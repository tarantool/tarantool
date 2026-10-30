local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'join'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[CREATE TABLE t1 (a INT PRIMARY KEY, b INT);]])
        box.execute([[CREATE TABLE t2 (a INT PRIMARY KEY, b INT);]])
        box.execute([[INSERT INTO t1 VALUES (1, 10), (2, 20);]])
        box.execute([[INSERT INTO t2 VALUES (1, 100), (3, 300);]])
    end)
end)

g.after_all(function()
    g.server:drop()
end)

--
-- Check valid and supported JOIN types.
--
g.test_valid_join_types = function()
    g.server:exec(function()
        local sql = [[SELECT t1.a, t2.a FROM t1 INNER JOIN t2 ON t1.a = t2.a;]]
        t.assert_equals(box.execute(sql).rows, {{1, 1}})

        sql = [[SELECT t1.a, t2.a FROM t1 CROSS JOIN t2 ON t1.a = t2.a;]]
        t.assert_equals(box.execute(sql).rows, {{1, 1}})

        sql = [[SELECT t1.a, t1.b, t2.b FROM t1 NATURAL JOIN t2;]]
        t.assert_equals(box.execute(sql).rows, {})

        sql = [[SELECT t1.a, t1.b, t2.b FROM t1 INNER NATURAL JOIN t2;]]
        t.assert_equals(box.execute(sql).rows, {})

        sql = [[SELECT t1.a, t1.b, t2.b FROM t1 CROSS NATURAL JOIN t2;]]
        t.assert_equals(box.execute(sql).rows, {})

        sql = [[SELECT t1.a, t2.a FROM t1 LEFT JOIN t2 ON t1.a = t2.a;]]
        t.assert_equals(box.execute(sql).rows, {{1, 1}, {2, box.NULL}})

        sql = [[SELECT t1.a, t2.a FROM t1 LEFT OUTER JOIN t2 ON t1.a = t2.a;]]
        t.assert_equals(box.execute(sql).rows, {{1, 1}, {2, box.NULL}})

        sql = [[SELECT t1.a, t1.b, t2.b FROM t1 NATURAL LEFT JOIN t2;]]
        local res = box.execute(sql)
        t.assert_equals(res.rows, {{1, 10, box.NULL}, {2, 20, box.NULL}})
    end)
end

--
-- Check invalid JOIN types.
--
g.test_join_cannot_be_both_inner_and_outer = function()
    g.server:exec(function()
        local exp_err = 'JOIN cannot be both OUTER and INNER'

        local sql = [[SELECT t1.a, t2.a FROM t1 INNER OUTER JOIN t2 ON
                      t1.a = t2.a;]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, exp_err)

        sql = [[SELECT t1.a, t2.a FROM t1 INNER LEFT JOIN t2 ON t1.a = t2.a;]]
        _, err = box.execute(sql)
        t.assert_equals(err.message, exp_err)

        sql = [[SELECT t1.a, t2.a FROM t1 INNER RIGHT JOIN t2 ON t1.a = t2.a;]]
        _, err = box.execute(sql)
        t.assert_equals(err.message, exp_err)
    end)
end

--
-- Check unsupported JOIN types.
--
g.test_join_unsupported_outer = function()
    g.server:exec(function()
        local exp_err = 'Tarantool does not support RIGHT and FULL OUTER JOINs'

        local sql = [[SELECT t1.a, t2.a FROM t1 OUTER JOIN t2 ON t1.a = t2.a;]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, exp_err)

        sql = [[SELECT t1.a, t2.a FROM t1 RIGHT JOIN t2 ON t1.a = t2.a;]]
        _, err = box.execute(sql)
        t.assert_equals(err.message, exp_err)

        sql = [[SELECT t1.a, t2.a FROM t1 RIGHT OUTER JOIN t2 ON t1.a = t2.a;]]
        _, err = box.execute(sql)
        t.assert_equals(err.message, exp_err)

        sql = [[SELECT t1.a, t2.a FROM t1 LEFT RIGHT JOIN t2 ON t1.a = t2.a;]]
        _, err = box.execute(sql)
        t.assert_equals(err.message, exp_err)
    end)
end
