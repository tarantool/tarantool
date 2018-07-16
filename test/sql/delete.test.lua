test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

-- box.cfg()

-- create space
box.sql.execute("CREATE TABLE t1(a, b, PRIMARY KEY(a, b));");

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
