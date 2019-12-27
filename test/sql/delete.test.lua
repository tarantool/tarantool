test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- box.cfg()

-- create space
box.execute("CREATE TABLE t1(a INT, b INT, PRIMARY KEY(a, b));");

-- Seed entries
box.execute("INSERT INTO t1 VALUES(1, 2);");
box.execute("INSERT INTO t1 VALUES(2, 4);");
box.execute("INSERT INTO t1 VALUES(1, 5);");

-- Two rows to be removed.
box.execute("DELETE FROM t1 WHERE a=1;");

-- Verify
box.execute("SELECT * FROM t1;");

-- Cleanup
box.execute("DROP TABLE t1;");

-- Debug
-- require("console").start()

--
-- gh-3535: Assertion with trigger and non existent table
--
box.execute("DELETE FROM t1;")

box.execute("CREATE TABLE t2 (s1 INT PRIMARY KEY);")
box.execute("CREATE TRIGGER t2 BEFORE INSERT ON t2 FOR EACH ROW BEGIN DELETE FROM t1; END;")
box.execute("INSERT INTO t2 VALUES (0);")

box.execute("DROP TABLE t2;")


--
-- gh-2201: TRUNCATE TABLE operation.
--

-- can't truncate system table.
box.execute("TRUNCATE TABLE \"_fk_constraint\";")

box.execute("CREATE TABLE t1(id INT PRIMARY KEY, a INT, b TEXT);")
box.execute("INSERT INTO t1 VALUES(1, 1, 'one');")
box.execute("INSERT INTO t1 VALUES(2, 2, 'two');")

-- Truncate rollback
box.execute("START TRANSACTION")
box.execute("TRUNCATE TABLE t1;")
box.execute("ROLLBACK")
box.execute("SELECT * FROM t1")

-- Can't truncate view.
box.execute("CREATE VIEW v1 AS SELECT * FROM t1;")
box.execute("TRUNCATE TABLE v1;")

-- Can't truncate table with FK.
box.execute("CREATE TABLE t2(x INT PRIMARY KEY REFERENCES t1(id));")
box.execute("TRUNCATE TABLE t1;")

-- Table triggers should be ignored.
box.execute("DROP TABLE t2;")
box.execute("CREATE TABLE t2(x INT PRIMARY KEY);")
box.execute("CREATE TRIGGER trig2 BEFORE DELETE ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES(old.x); END;")
box.execute("TRUNCATE TABLE t1;")
box.execute("SELECT * FROM t1;")
box.execute("SELECT * FROM t2;")

-- Cleanup.
box.execute("DROP VIEW v1");
box.execute("DROP TABLE t1;")
box.execute("DROP TABLE t2;")
