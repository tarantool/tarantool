local server = require('luatest.server')
local t = require('luatest')

local g = t.group("restart", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[SET SESSION "sql_default_engine" = '%s';]], {engine})
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_2828_inline_unique_presistency_check = function(cg)
    cg.server:exec(function()
        -- Create a table and insert a datum
        box.execute([[CREATE TABLE t1(a INT PRIMARY KEY, b INT, UNIQUE(b));]])
        box.execute([[INSERT INTO t1 VALUES(1,2);]])

        -- Sanity check
        local res = box.execute([[SELECT * FROM SEQSCAN t1;]])
        t.assert_equals(res.rows, {{1, 2}})
    end)

    cg.server:restart()
    cg.server:exec(function()
        -- This cmd should not fail
        -- before this fix, unique index was notrecovered
        -- correctly after restart (#2808)
        box.execute([[INSERT INTO t1 VALUES(2,3);]])

        -- Sanity check
        local res = box.execute([[SELECT * FROM SEQSCAN t1;]])
        t.assert_equals(res.rows, {{1, 2}, {2, 3}})

        -- Cleanup
        box.execute([[DROP TABLE t1;]])
    end)
end
