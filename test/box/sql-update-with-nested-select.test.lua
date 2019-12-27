test_run = require('test_run').new()

-- box.cfg()

-- create space
box.execute("CREATE TABLE t1(a integer primary key, b int UNIQUE, e int);");

-- Seed entries
box.execute("INSERT INTO t1 VALUES(1,4,6);");
box.execute("INSERT INTO t1 VALUES(2,5,7);");

-- Both entries must be updated
box.execute("UPDATE t1 SET e=e+1 WHERE b IN (SELECT b FROM t1);");

-- Check
box.execute("SELECT e FROM t1");

-- Cleanup
box.execute("DROP TABLE t1;");

-- Debug
-- require("console").start()
