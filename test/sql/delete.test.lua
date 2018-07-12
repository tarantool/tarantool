test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

-- box.cfg()

-- create space
box.sql.execute("CREATE TABLE t1(a INT, b INT, PRIMARY KEY(a, b));");

-- Debug
-- box.sql.execute("PRAGMA vdbe_debug=ON ; INSERT INTO zoobar VALUES (111, 222, 'c3', 444)")

-- Seed entries
box.sql.execute("INSERT INTO t1 VALUES(1, 2);");
box.sql.execute("INSERT INTO t1 VALUES(2, 4);");
box.sql.execute("INSERT INTO t1 VALUES(1, 5);");

-- Two rows to be removed.
box.sql.execute("DELETE FROM t1 WHERE a=1;");

-- Verify
box.sql.execute("SELECT * FROM t1;");

-- Cleanup
box.sql.execute("DROP TABLE t1;");

-- Debug
-- require("console").start()

--
-- gh-3535: Assertion with trigger and non existent table
--
box.sql.execute("DELETE FROM t1;")

box.sql.execute("CREATE TABLE t2 (s1 INT PRIMARY KEY);")
box.sql.execute("CREATE TRIGGER t2 BEFORE INSERT ON t2 BEGIN DELETE FROM t1; END;")
box.sql.execute("INSERT INTO t2 VALUES (0);")

box.sql.execute("DROP TABLE t2;")


--
-- gh-2201: TRUNCATE TABLE operation.
--

-- can't truncate system table.
box.sql.execute("TRUNCATE TABLE \"_sql_stat1\";")

box.sql.execute("CREATE TABLE t1(id INT PRIMARY KEY, a INT, b TEXT);")
box.sql.execute("INSERT INTO t1 VALUES(1, 1, 'one');")
box.sql.execute("INSERT INTO t1 VALUES(2, 2, 'two');")

-- Can't truncate in transaction.
box.sql.execute("START TRANSACTION")
box.sql.execute("TRUNCATE TABLE t1;")
box.sql.execute("ROLLBACK")

-- Can't truncate view.
box.sql.execute("CREATE VIEW v1 AS SELECT * FROM t1;")
box.sql.execute("TRUNCATE TABLE v1;")

-- Can't truncate table with FK.
box.sql.execute("CREATE TABLE t2(x INT PRIMARY KEY REFERENCES t1(id));")
box.sql.execute("TRUNCATE TABLE t1;")

-- Table triggers should be ignored.
box.sql.execute("DROP TABLE t2;")
box.sql.execute("CREATE TABLE t2(x INT PRIMARY KEY);")
box.sql.execute("CREATE TRIGGER trig2 BEFORE DELETE ON t1 BEGIN INSERT INTO t2 VALUES(old.x); END;")
box.sql.execute("TRUNCATE TABLE t1;")
box.sql.execute("SELECT * FROM t1;")
box.sql.execute("SELECT * FROM t2;")

-- Cleanup.
box.sql.execute("DROP VIEW v1");
box.sql.execute("DROP TABLE t1;")
box.sql.execute("DROP TABLE t2;")
