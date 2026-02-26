local server = require('luatest.server')
local t = require('luatest')

local g = t.group("check", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--This test checks the correctness of working with ephemeral tables
g.test_check_clear_ephemeral = function(cg)
    cg.server:exec(function()
        -- create space
        box.execute([[CREATE TABLE t1(a INT,b INT,c INT,PRIMARY KEY(b,c));]])

        -- Seed entries
        box.execute([[WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1
                                                FROM cnt WHERE x<1000)
                      INSERT INTO t1 SELECT x, x%40, x/40 FROM cnt;]])

        -- Ephemeral table is not belong to Tarantool, so must be cleared sql-way.
        local exp = {{840}, {880}, {920}, {960}, {1000},
                     {1}, {41}, {81}, {121}, {161}}
        local res = box.execute([[SELECT a FROM t1 ORDER BY b, a LIMIT 10 OFFSET 20;]])
        t.assert_equals(res.rows, exp)

        -- Cleanup
        box.execute([[DROP TABLE t1;]])
    end)
end
