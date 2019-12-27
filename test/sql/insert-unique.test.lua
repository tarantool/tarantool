test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- box.cfg()

-- create space
box.execute("CREATE TABLE zoobar (c1 INT, c2 INT PRIMARY KEY, c3 TEXT, c4 INT)")
box.execute("CREATE UNIQUE INDEX zoobar2 ON zoobar(c1, c4)")

-- Seed entry
box.execute("INSERT INTO zoobar VALUES (111, 222, 'c3', 444)")

-- PK must be unique
box.execute("INSERT INTO zoobar VALUES (112, 222, 'c3', 444)")

-- Unique index must be respected
box.execute("INSERT INTO zoobar VALUES (111, 223, 'c3', 444)")

-- Cleanup
box.execute("DROP INDEX zoobar2 ON zoobar")
box.execute("DROP TABLE zoobar")

-- Debug
-- require("console").start()
