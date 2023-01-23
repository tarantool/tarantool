local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_uint_int'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[CREATE TABLE t(i UNSIGNED PRIMARY KEY, a UNSIGNED);]])
        box.execute([[INSERT INTO t VALUES (1, 2);]])
    end)
end)

g.after_all(function()
    g.server:exec(function()
        box.execute([[DROP TABLE t;]])
    end)
    g.server:stop()
end)

g.test_uint_int_1 = function()
    g.server:exec(function()
        local res = {{name = "COLUMN_1", type = "integer"}}
        t.assert_equals(box.execute([[SELECT i + a FROM t;]]).metadata, res)
        t.assert_equals(box.execute([[SELECT i - a FROM t;]]).metadata, res)
        t.assert_equals(box.execute([[SELECT i * a FROM t;]]).metadata, res)
        t.assert_equals(box.execute([[SELECT i / a FROM t;]]).metadata, res)
        t.assert_equals(box.execute([[SELECT i % a FROM t;]]).metadata, res)
        t.assert_equals(box.execute([[SELECT i & a FROM t;]]).metadata, res)
        t.assert_equals(box.execute([[SELECT i | a FROM t;]]).metadata, res)
        t.assert_equals(box.execute([[SELECT i >> a FROM t;]]).metadata, res)
        t.assert_equals(box.execute([[SELECT i << a FROM t;]]).metadata, res)
    end)
end

g.test_uint_int_2 = function()
    g.server:exec(function()
        local sql = [[SELECT i - a FROM t ORDER BY 1 LIMIT 1;]]
        t.assert_equals(box.execute(sql).rows, {{-1}})
    end)
end
