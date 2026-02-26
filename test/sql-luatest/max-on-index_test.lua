local server = require('luatest.server')
local t = require('luatest')

local g = t.group("max", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

--This test checks the correctness of the MAX() function
g.test_max_on_index = function(cg)
    cg.server:exec(function()
        -- create space
        -- scalar affinity
        box.execute([[CREATE TABLE test1 (f1 INT, f2 INT, PRIMARY KEY(f1));]])
        box.execute([[CREATE INDEX test1_index ON test1 (f2);]])

        -- integer affinity
        box.execute([[CREATE TABLE test2 (f1 INT, f2 INT, PRIMARY KEY(f1));]])

        -- Seed entries
        box.execute([[INSERT INTO test1 VALUES(1, 2);]])
        box.execute([[INSERT INTO test1 VALUES(2, NULL);]])
        box.execute([[INSERT INTO test1 VALUES(3, NULL);]])
        box.execute([[INSERT INTO test1 VALUES(4, 3);]])

        box.execute([[INSERT INTO test2 VALUES(1, 2);]])

        -- Select must return properly decoded `NULL`
        local res = box.execute([[SELECT MAX(f1) FROM test1;]])
        t.assert_equals(res.rows, {{4}})

        res = box.execute([[SELECT MAX(f2) FROM test1;]])
        t.assert_equals(res.rows, {{3}})

        res = box.execute([[SELECT MAX(f1) FROM test2;]])
        t.assert_equals(res.rows, {{1}})

        -- Cleanup
        box.execute([[DROP INDEX test1_index ON test1;]])
        box.execute([[DROP TABLE test1;]])
        box.execute([[DROP TABLE test2;]])
    end)
end
