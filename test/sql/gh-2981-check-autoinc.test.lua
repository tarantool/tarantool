test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

box.cfg{}

box.execute("CREATE TABLE t1 (s1 INTEGER PRIMARY KEY AUTOINCREMENT, s2 INTEGER, CHECK (s1 <> 19));");
box.execute("CREATE TABLE t2 (s1 INTEGER PRIMARY KEY AUTOINCREMENT, s2 INTEGER, CHECK (s1 <> 19 AND s1 <> 25));");
box.execute("CREATE TABLE t3 (s1 INTEGER PRIMARY KEY AUTOINCREMENT, s2 INTEGER, CHECK (s1 < 10));");

box.execute("insert into t1 values (18, null);")
box.execute("insert into t1(s2) values (null);")

box.execute("insert into t2 values (18, null);")
box.execute("insert into t2(s2) values (null);")
box.execute("insert into t2 values (24, null);")
box.execute("insert into t2(s2) values (null);")

box.execute("insert into t3 values (9, null)")
box.execute("insert into t3(s2) values (null)")

box.execute("DROP TABLE t1")
box.execute("DROP TABLE t2")
box.execute("DROP TABLE t3")

