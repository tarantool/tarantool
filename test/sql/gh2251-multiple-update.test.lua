-- Regression test for #2251
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

-- box.cfg()

box.sql.execute("CREATE TABLE t1(a integer primary key, b INT UNIQUE, e INT);")
box.sql.execute("INSERT INTO t1 VALUES(1,4,6);")
box.sql.execute("INSERT INTO t1 VALUES(2,5,7);")

box.sql.execute("UPDATE t1 SET e=e+1 WHERE b IN (SELECT b FROM t1);")

box.sql.execute("SELECT e FROM t1")

box.sql.execute("CREATE TABLE t2(a integer primary key, b INT UNIQUE, c NUM, d NUM, e INT,  UNIQUE(c,d));")
box.sql.execute("INSERT INTO t2 VALUES(1,2,3,4,5);")
box.sql.execute("INSERT INTO t2 VALUES(2,3,4,4,6);")

box.sql.execute("UPDATE t2 SET e=e+1 WHERE b IN (SELECT b FROM t2);")

box.sql.execute("SELECT e FROM t2")

box.sql.execute("DROP TABLE t1")
box.sql.execute("DROP TABLE t2")

-- Debug
-- require("console").start()
