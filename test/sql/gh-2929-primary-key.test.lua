test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

-- All tables in SQL are now WITHOUT ROW ID, so if user
-- tries to create table without a primary key, an appropriate error message
-- should be raised. This tests checks it.

box.cfg{}

box.sql.execute("CREATE TABLE t1(a INT PRIMARY KEY, b INT UNIQUE)")
box.sql.execute("CREATE TABLE t2(a INT UNIQUE, b INT)")

box.sql.execute("CREATE TABLE t3(a NUM)")
box.sql.execute("CREATE TABLE t4(a DECIMAL, b TEXT)")
box.sql.execute("CREATE TABLE t5(a DECIMAL, b NUM UNIQUE)")

box.sql.execute("DROP TABLE t1")

--
-- gh-3522: invalid primary key name
--
box.sql.execute("CREATE TABLE tx (a INT, PRIMARY KEY (b));")
