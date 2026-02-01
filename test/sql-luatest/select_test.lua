local server = require('luatest.server')
local t = require('luatest')

local g = t.group("select", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- This test checks the correct handling of NULL values in SQL queries.
--
g.test_select_null = function(cg)
    cg.server:exec(function()
        -- Create space.
        local sql = [[CREATE TABLE t3
                      (id INT, a TEXT, b TEXT, PRIMARY KEY(id));]]
        box.execute(sql)

        -- Seed entries.
        box.execute([[INSERT INTO t3 VALUES (1, 'abc', NULL);]]);
        box.execute([[INSERT INTO t3 VALUES (2, NULL, 'xyz');]]);

        -- Select must return properly decoded `NULL`.
        local exp = {
            {1, 'abc', nil},
            {2, nil, 'xyz'},
        }
        local res = box.execute([[SELECT * FROM t3;]])
        t.assert_equals(res.rows, exp)

        -- Clean up.
        box.execute([[DROP TABLE t3;]])
    end)
end

g = t.group("dup")

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- No duplicate column error for a select
g.test_12337_throw_duplicate_from_select = function(cg)
    cg.server:exec(function()
        local sql = [[SELECT * FROM (SELECT 1 as a, 2 as a, 3 as c);]]
        local _, err = box.execute(sql)
        local exp_err = "ambiguous column name: a"
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[SELECT a FROM (SELECT 1 as a, 2 as a, 3 as c);]])
        t.assert_equals(tostring(err), exp_err)

        sql = [[SELECT c FROM (SELECT 1 as a, 2 as a, 3 as c);]]
        local res = box.execute(sql)
        t.assert_equals(res.rows, {{3}})

        _, err = box.execute([[SELECT a FROM (SELECT 1 as A, 2 as A, 3 as c);]])
        t.assert_equals(tostring(err), exp_err)

        res = box.execute([[SELECT * FROM (SELECT 1 as a, 2 as A, 3 as c);]])
        t.assert_equals(res.rows, {{1, 2, 3}})
    end)
end
