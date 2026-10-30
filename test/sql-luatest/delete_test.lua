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

g.test_delete_where = function(cg)
    cg.server:exec(function()
        -- Create space.
        box.execute("CREATE TABLE t1(a INT, b INT, PRIMARY KEY(a, b));")

        -- Seed entries.
        box.execute("INSERT INTO t1 VALUES(1, 2);")
        box.execute("INSERT INTO t1 VALUES(2, 4);")
        box.execute("INSERT INTO t1 VALUES(1, 5);")

        -- Two rows to be removed.
        local res = box.execute("DELETE FROM t1 WHERE a = 1;")
        t.assert_equals(res, {row_count = 2})

        -- Verify.
        res = box.execute("SELECT * FROM t1;")
        t.assert_equals(res.rows, {{2, 4}})

        -- Cleanup.
        box.execute("DROP TABLE t1;")
    end)
end

--
-- gh-3535: Assertion with trigger and non existent table.
--
g.test_3535_trigger_and_non_existent_table = function (cg)
    cg.server:exec(function ()
        local _, err = box.execute("DELETE FROM t1;")
        t.assert_equals(err.message, "Space 't1' does not exist")

        box.execute("CREATE TABLE t2 (s1 INT PRIMARY KEY);")
        box.execute([[CREATE TRIGGER t2 BEFORE INSERT ON t2
                      FOR EACH ROW BEGIN DELETE FROM t1; END;]])
        _, err = box.execute("INSERT INTO t2 VALUES (0);")
        t.assert_equals(err.message, "Space 't1' does not exist")

        box.execute("DROP TABLE t2;")
    end)
end

--
-- gh-2201: TRUNCATE TABLE operation.
--
g.test_2201_truncate_table = function (cg)
    cg.server:exec(function ()
        -- Can't truncate system table.
        local _, err = box.execute('TRUNCATE TABLE "_fk_constraint";')
        local exp_err = "Can't truncate a system space, space '_fk_constraint'"
        t.assert_equals(err.message, exp_err)

        box.execute("CREATE TABLE t1(id INT PRIMARY KEY, a INT, b TEXT);")
        box.execute("INSERT INTO t1 VALUES(1, 1, 'one');")
        box.execute("INSERT INTO t1 VALUES(2, 2, 'two');")

        -- Truncate rollback.
        box.execute("START TRANSACTION;")
        box.execute("TRUNCATE TABLE t1;")
        box.execute("ROLLBACK;")
        local res = box.execute("SELECT * FROM t1;")
        t.assert_equals(res.rows, {{1, 1, "one"}, {2, 2, "two"}})

        -- Can't truncate view.
        box.execute("CREATE VIEW v1 AS SELECT * FROM t1;")
        _, err = box.execute("TRUNCATE TABLE v1;")
        exp_err = "Failed to execute SQL statement: " ..
                  "can not truncate space 'v1' because space is a view"
        t.assert_equals(err.message, exp_err)

        -- Can't truncate table with FK.
        box.execute("CREATE TABLE t2(x INT PRIMARY KEY REFERENCES t1(id));")
        box.execute("INSERT INTO t2 VALUES(1);")
        _, err = box.execute("TRUNCATE TABLE t1;")
        exp_err = "Can't modify space 't1': space is referenced by foreign key"
        t.assert_equals(err.message, exp_err)

        -- Table triggers should be ignored.
        box.execute("DROP TABLE t2;")
        box.execute("CREATE TABLE t2(x INT PRIMARY KEY);")
        box.execute([[CREATE TRIGGER trig2 BEFORE DELETE ON t1
                      FOR EACH ROW BEGIN INSERT INTO t2 VALUES(OLD.x); END;]])
        res = box.execute("TRUNCATE TABLE t1;")
        t.assert_equals(res, {row_count = 0})
        res = box.execute("SELECT * FROM t1;")
        t.assert_equals(res.rows, {})
        res = box.execute("SELECT * FROM t2;")
        t.assert_equals(res.rows, {})

        -- Cleanup.
        box.execute("DROP VIEW v1;")
        box.execute("DROP TABLE t1;")
        box.execute("DROP TABLE t2;")
    end)
end
