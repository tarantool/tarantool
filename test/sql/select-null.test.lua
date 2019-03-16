test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')

-- box.cfg()

-- create space
box.execute("CREATE TABLE t3(id INT, a text, b TEXT, PRIMARY KEY(id))")

-- Debug
-- box.execute("PRAGMA vdbe_debug=ON ; INSERT INTO zoobar VALUES (111, 222, 'c3', 444)")

-- Seed entries
box.execute("INSERT INTO t3 VALUES(1, 'abc',NULL)");
box.execute("INSERT INTO t3 VALUES(2, NULL,'xyz')");

-- Select must return properly decoded `NULL`
box.execute("SELECT * FROM t3")

-- Cleanup
box.execute("DROP TABLE t3")

-- Debug
-- require("console").start()
