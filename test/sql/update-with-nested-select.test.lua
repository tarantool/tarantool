test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- box.cfg()

-- create space
box.execute("CREATE TABLE t1(a integer primary key, b INT UNIQUE, e INT);");

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
