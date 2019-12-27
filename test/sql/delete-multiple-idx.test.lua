test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- box.cfg()

-- Create space.
box.execute("CREATE TABLE t3(id INT primary key,x INT,y INT);");
box.execute("CREATE UNIQUE INDEX t3y ON t3(y);");

-- Seed entries.
box.execute("INSERT INTO t3 VALUES (1, 1, NULL);");
box.execute("INSERT INTO t3 VALUES(2,9,NULL);");
box.execute("INSERT INTO t3 VALUES(3,5,NULL);");
box.execute("INSERT INTO t3 VALUES(6, 234,567);");


-- Delete should be done from both trees..
box.execute("DELETE FROM t3 WHERE y IS NULL;");

-- Verify.
box.execute("SELECT * FROM t3;");

-- Cleanup.
box.execute("DROP INDEX t3y ON t3");
box.execute("DROP TABLE t3;");

-- Debug.
-- require("console").start()




