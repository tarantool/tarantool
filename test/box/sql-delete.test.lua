test_run = require('test_run').new()

-- box.cfg()

-- create space
box.sql.execute("CREATE TABLE t1(a, b, PRIMARY KEY(a, b)) WITHOUT ROWID;");

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
