test_run = require('test_run').new()

-- box.cfg()

-- create space
box.sql.execute("CREATE TABLE t1(a,b,c,PRIMARY KEY(b,c));")

-- Debug
-- box.sql.execute("PRAGMA vdbe_debug=ON ; INSERT INTO zoobar VALUES (111, 222, 'c3', 444)")

-- Seed entries
box.sql.execute("WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<1000) INSERT INTO t1 SELECT x, x%40, x/40 FROM cnt;")

-- Ephemeral table is not belong to Tarantool, so must be cleared SQLite-way.
box.sql.execute("SELECT a FROM t1 ORDER BY b, a LIMIT 10 OFFSET 20;");

-- Cleanup
box.sql.execute("DROP TABLE t1")

-- Debug
-- require("console").start()
