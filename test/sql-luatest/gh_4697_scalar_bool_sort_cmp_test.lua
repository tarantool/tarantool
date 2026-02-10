local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end)
end)

g.after_each(function()
    g.server:drop()
end)

--
-- gh-4679: Make sure that boolean precedes any number within
-- scalar. Result with order by indexed (using index) and
-- non-indexed (using no index) must be the same.
--
g.test_gh4697_scalar_bool_sort_cmp = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE test (s1 INTEGER PRIMARY KEY,
                                         s2 SCALAR UNIQUE, s3 SCALAR);]])
        box.execute([[INSERT INTO test VALUES (0, 1, 1);]])
        box.execute([[INSERT INTO test VALUES (1, 1.1, 1.1);]])
        box.execute([[INSERT INTO test VALUES (2, true, true);]])
        box.execute([[INSERT INTO test VALUES (3, NULL, NULL);]])

        local res = box.execute([[SELECT s2, TYPEOF(s2)
                                  FROM SEQSCAN test ORDER BY s2;]])
        local exp = {
            {nil, 'NULL'},
            {true, 'scalar'},
            {1, 'scalar'},
            {1.1, 'scalar'},
        }
        t.assert_equals(res.rows, exp)
        res = box.execute([[SELECT s3, TYPEOF(s3)
                            FROM SEQSCAN test ORDER BY s3;]])
        t.assert_equals(res.rows, exp)
    end)
end
