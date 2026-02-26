local server = require('luatest.server')
local t = require('luatest')

local g = t.group("boolean", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-4697: Make sure that boolean precedes any number within
-- scalar. Result with order by indexed (using index) and
-- non-indexed (using no index) must be the same.
--
g.test_4697_scalar_bool_sort_cmp = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE test (s1 INTEGER PRIMARY KEY,
                                         s2 SCALAR UNIQUE, s3 SCALAR);]])
        box.execute([[INSERT INTO test VALUES (0, 1, 1);]])
        box.execute([[INSERT INTO test VALUES (1, 1.1, 1.1);]])
        box.execute([[INSERT INTO test VALUES (2, True, True);]])
        box.execute([[INSERT INTO test VALUES (3, NULL, NULL);]])

        local exp = {
            {nil, 'NULL'},
            {true, 'scalar'},
            {1, 'scalar'},
            {1.1, 'scalar'},
        }
        local sql = "SELECT s2, TYPEOF(s2) FROM SEQSCAN test ORDER BY s2;"
        t.assert_equals(box.execute(sql).rows, exp)
        sql = "SELECT s3, TYPEOF(s3) FROM SEQSCAN test ORDER BY s3;"
        t.assert_equals(box.execute(sql).rows, exp)
    end)
end
