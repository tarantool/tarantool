test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- All tables in SQL are now WITHOUT ROW ID, so if user
-- tries to create table without a primary key, an appropriate error message
-- should be raised. This tests checks it.

box.cfg{}

box.execute("CREATE TABLE t1(a INT PRIMARY KEY, b INT UNIQUE)")
box.execute("CREATE TABLE t2(a INT UNIQUE, b INT)")

box.execute("CREATE TABLE t3(a NUMBER)")
box.execute("CREATE TABLE t4(a NUMBER, b TEXT)")
box.execute("CREATE TABLE t5(a NUMBER, b NUMBER UNIQUE)")

box.execute("DROP TABLE t1")

--
-- gh-3522: invalid primary key name
--
box.execute("CREATE TABLE tx (a INT, PRIMARY KEY (b));")
