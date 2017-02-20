test_run = require('test_run').new()

-- box.cfg()

-- create space
box.sql.execute("CREATE TABLE zzoobar (c1, c2 PRIMARY KEY, c3, c4) WITHOUT ROWID")

box.sql.execute("CREATE UNIQUE INDEX zoobar2 ON zzoobar(c1, c4)")
box.sql.execute("CREATE        INDEX zoobar3 ON zzoobar(c3)")

-- Debug
-- box.sql.execute("PRAGMA vdbe_debug=ON ; INSERT INTO zzoobar VALUES (111, 222, 'c3', 444)")

-- Dummy entry
box.sql.execute("INSERT INTO zzoobar VALUES (111, 222, 'c3', 444)")

box.sql.execute("DROP INDEX zoobar2")
box.sql.execute("DROP INDEX zoobar3")

-- zoobar2 is dropped - should be OK
box.sql.execute("INSERT INTO zzoobar VALUES (111, 223, 'c3', 444)")

-- zoobar2 was dropped. Re-creation should  be OK
box.sql.execute("CREATE INDEX zoobar2 ON zzoobar(c3)")

-- Cleanup
box.sql.execute("DROP INDEX zoobar2")
box.sql.execute("DROP TABLE zzoobar")

-- Debug
-- require("console").start()
