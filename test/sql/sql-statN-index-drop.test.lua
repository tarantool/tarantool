test_run = require('test_run').new()

-- Initializing some things.
box.sql.execute("CREATE TABLE t1(id PRIMARY KEY, a);")
box.sql.execute("CREATE TABLE t2(id PRIMARY KEY, a);")
box.sql.execute("CREATE INDEX i1 ON t1(a);")
box.sql.execute("CREATE INDEX i1 ON t2(a);")
box.sql.execute("INSERT INTO t1 VALUES(1, 2);")
box.sql.execute("INSERT INTO t2 VALUES(1, 2);")

-- Analyze.
box.sql.execute("ANALYZE;")

-- Checking the data.
box.sql.execute("SELECT * FROM \"_sql_stat4\";")
box.sql.execute("SELECT * FROM \"_sql_stat1\";")

-- Dropping an index.
box.sql.execute("DROP INDEX i1 ON t1;")

-- Checking the DROP INDEX results.
box.sql.execute("SELECT * FROM \"_sql_stat4\";")
box.sql.execute("SELECT * FROM \"_sql_stat1\";")

--Cleaning up.
box.sql.execute("DROP TABLE t1;")
box.sql.execute("DROP TABLE t2;")

-- Same test but dropping an INDEX ON t2.

box.sql.execute("CREATE TABLE t1(id PRIMARY KEY, a);")
box.sql.execute("CREATE TABLE t2(id PRIMARY KEY, a);")
box.sql.execute("CREATE INDEX i1 ON t1(a);")
box.sql.execute("CREATE INDEX i1 ON t2(a);")
box.sql.execute("INSERT INTO t1 VALUES(1, 2);")
box.sql.execute("INSERT INTO t2 VALUES(1, 2);")

-- Analyze.
box.sql.execute("ANALYZE;")

-- Checking the data.
box.sql.execute("SELECT * FROM \"_sql_stat4\";")
box.sql.execute("SELECT * FROM \"_sql_stat1\";")

-- Dropping an index.
box.sql.execute("DROP INDEX i1 ON t2;")

-- Checking the DROP INDEX results.
box.sql.execute("SELECT * FROM \"_sql_stat4\";")
box.sql.execute("SELECT * FROM \"_sql_stat1\";")

--Cleaning up.
box.sql.execute("DROP TABLE t1;")
box.sql.execute("DROP TABLE t2;")
