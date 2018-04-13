test_run = require('test_run').new()

-- Create space
box.sql.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT ABORT)")
box.sql.execute("CREATE TABLE q (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT FAIL)")
box.sql.execute("CREATE TABLE p (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT IGNORE)")
box.sql.execute("CREATE TABLE e (id INTEGER PRIMARY KEY ON CONFLICT REPLACE, v INTEGER)")

-- Insert values and select them
box.sql.execute("INSERT INTO t values (1, 1), (2, 2), (3, 1)")
box.sql.execute("SELECT * FROM t")

box.sql.execute("INSERT INTO q values (1, 1), (2, 2), (3, 1)")
box.sql.execute("SELECT * FROM q")

box.sql.execute("INSERT INTO p values (1, 1), (2, 2), (3, 1), (4, 5)")
box.sql.execute("SELECT * FROM p")

box.sql.execute("INSERT INTO e values (1, 1), (2, 2), (1, 3)")
box.sql.execute("SELECT * FROM e")

box.sql.execute("CREATE TABLE t1(a INT PRIMARY KEY ON CONFLICT REPLACE)")
box.sql.execute("INSERT INTO t1 VALUES (9)")
box.sql.execute("INSERT INTO t1 VALUES (9)")
box.sql.execute("SELECT * FROM t1")

box.sql.execute("CREATE TABLE t2(a INT PRIMARY KEY ON CONFLICT IGNORE)")
box.sql.execute("INSERT INTO t2 VALUES (9)")
box.sql.execute("INSERT INTO t2 VALUES (9)")

box.sql.execute("SELECT * FROM t2")

box.sql.execute('DROP TABLE t')
box.sql.execute('DROP TABLE q')
box.sql.execute('DROP TABLE p')
box.sql.execute('DROP TABLE e')
box.sql.execute('DROP TABLE t1')
box.sql.execute('DROP TABLE t2')
