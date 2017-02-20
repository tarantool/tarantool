test_run = require('test_run').new()

-- box.cfg()

-- create space
box.sql.execute("CREATE TABLE zoobar (c1, c2 PRIMARY KEY, c3, c4) WITHOUT ROWID")
box.sql.execute("CREATE UNIQUE INDEX zoobar2 ON zoobar(c1, c4)")

-- Debug
-- box.sql.execute("PRAGMA vdbe_debug=ON ; INSERT INTO zoobar VALUES (111, 222, 'c3', 444)")

-- Seed entry
box.sql.execute("INSERT INTO zoobar VALUES (111, 222, 'c3', 444)")

-- PK must be unique
box.sql.execute("INSERT INTO zoobar VALUES (112, 222, 'c3', 444)")

-- Unique index must be respected
box.sql.execute("INSERT INTO zoobar VALUES (111, 223, 'c3', 444)")

-- Cleanup
box.sql.execute("DROP INDEX zoobar2")
box.space.zoobar:drop()

-- Debug
-- require("console").start()
