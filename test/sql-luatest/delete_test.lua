local server = require('luatest.server')
local t = require('luatest')

local g = t.group("delete", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_delete = function(cg)
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
        local res, err = box.execute("DELETE FROM t1;")
        t.assert_equals(err.message, "Space 't1' does not exist")

        box.execute("CREATE TABLE t2 (s1 INT PRIMARY KEY);")
        box.execute([[CREATE TRIGGER t2 BEFORE INSERT ON t2
                      FOR EACH ROW BEGIN DELETE FROM t1; END;]])
        res, err = box.execute("INSERT INTO t2 VALUES (0);")
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
        local _, err = box.execute("TRUNCATE TABLE \"_fk_constraint\";")
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
                      FOR EACH ROW BEGIN INSERT INTO t2 VALUES(old.x); END;]])
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
