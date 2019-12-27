test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- box.cfg()

-- create space
box.execute("CREATE TABLE t3(id INT, a text, b TEXT, PRIMARY KEY(id))")

-- Seed entries
box.execute("INSERT INTO t3 VALUES(1, 'abc',NULL)");
box.execute("INSERT INTO t3 VALUES(2, NULL,'xyz')");

-- Select must return properly decoded `NULL`
box.execute("SELECT * FROM t3")

-- Cleanup
box.execute("DROP TABLE t3")

-- Debug
-- require("console").start()
