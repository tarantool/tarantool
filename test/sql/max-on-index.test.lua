test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- box.cfg()

-- create space
-- scalar affinity
box.execute("CREATE TABLE test1 (f1 INT, f2 INT, PRIMARY KEY(f1))")
box.execute("CREATE INDEX test1_index ON test1 (f2)")

-- integer affinity
box.execute("CREATE TABLE test2 (f1 INT, f2 INT, PRIMARY KEY(f1))")

-- Seed entries
box.execute("INSERT INTO test1 VALUES(1, 2)");
box.execute("INSERT INTO test1 VALUES(2, NULL)");
box.execute("INSERT INTO test1 VALUES(3, NULL)");
box.execute("INSERT INTO test1 VALUES(4, 3)");

box.execute("INSERT INTO test2 VALUES(1, 2)");

-- Select must return properly decoded `NULL`
box.execute("SELECT MAX(f1) FROM test1")
box.execute("SELECT MAX(f2) FROM test1")

box.execute("SELECT MAX(f1) FROM test2")

-- Cleanup
box.execute("DROP INDEX test1_index ON test1")
box.execute("DROP TABLE test1")
box.execute("DROP TABLE test2")

-- Debug
-- require("console").start()
