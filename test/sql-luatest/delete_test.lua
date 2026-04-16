local server = require('luatest.server')
local t = require('luatest')

local g = t.group("delete", {{engine = 'memtx'}, {engine = 'vinyl'}})

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
-- This test checks the correctness of deleting records
-- from a table with multiple indexes.
--
g.test_delete_multiple_idx = function(cg)
    cg.server:exec(function()
        -- Create space.
        box.execute([[CREATE TABLE t3 (id INT primary key, x INT, y INT);]])
        box.execute([[CREATE UNIQUE INDEX t3y ON t3(y);]])

        -- Seed entries.
        box.execute([[INSERT INTO t3 VALUES (1, 1, NULL);]])
        box.execute([[INSERT INTO t3 VALUES (2, 9, NULL);]])
        box.execute([[INSERT INTO t3 VALUES (3, 5, NULL);]])
        box.execute([[INSERT INTO t3 VALUES (6, 234, 567);]])

        -- Delete should be done from both trees.
        box.execute([[DELETE FROM t3 WHERE y IS NULL;]])

        -- Verify.
        local res = box.execute([[SELECT * FROM t3;]])
        t.assert_equals(res.rows, {{6, 234, 567}})

        -- Cleanup.
        box.execute([[DROP TABLE t3;]])
    end)
end

g.test_delete = function(cg)
    cg.server:exec(function()
        -- Create space.
        box.execute([[CREATE TABLE zoobar (c1 INT, c2 INT PRIMARY KEY, c3 TEXT,
                                           c4 INT);]])
        box.execute("CREATE UNIQUE INDEX zoobar2 ON zoobar(c1, c4);")

        -- Seed entry.
        local sql = "INSERT INTO zoobar VALUES (%d, %d, 'c3', 444);"
        local exp = {}
        for i = 1, 100 do
            table.insert(exp, {2 * i, i, 'c3', 444})
            box.execute(sql:format(2 * i, i))
        end

        -- Check table is not empty.
        t.assert_equals(box.execute("SELECT * FROM zoobar;").rows, exp)

        -- Do clean up.
        t.assert_equals(box.execute("DELETE FROM zoobar;"), {row_count = 100})

        -- Make sure table is empty.
        t.assert_equals(box.execute("SELECT * FROM zoobar;").rows, {})

        -- Cleanup.
        box.execute("DROP INDEX zoobar2 ON zoobar;")
        box.execute("DROP TABLE zoobar;")
    end)
end
