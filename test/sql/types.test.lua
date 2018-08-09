env = require('test_run')
test_run = env.new()

-- gh-3018: typeless columns are prohibited.
--
box.sql.execute("CREATE TABLE t1 (id PRIMARY KEY);")
box.sql.execute("CREATE TABLE t1 (a, id INT PRIMARY KEY);")
box.sql.execute("CREATE TABLE t1 (id PRIMARY KEY, a INT);")
box.sql.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a);")
box.sql.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a INT, b UNIQUE);")

-- gh-3104: real type is stored in space format.
--
box.sql.execute("CREATE TABLE t1 (id TEXT PRIMARY KEY, a REAL, b INT, c TEXT, d BLOB);")
box.space.T1:format()
box.sql.execute("CREATE VIEW v1 AS SELECT b + a, b - a FROM t1;")
box.space.V1:format()

-- gh-2494: index's part also features correct declared type.
--
box.sql.execute("CREATE INDEX i1 ON t1 (a);")
box.sql.execute("CREATE INDEX i2 ON t1 (b);")
box.sql.execute("CREATE INDEX i3 ON t1 (c);")
box.sql.execute("CREATE INDEX i4 ON t1 (id, c, b, a, d);")
box.space.T1.index.I1.parts
box.space.T1.index.I2.parts
box.space.T1.index.I3.parts
box.space.T1.index.I4.parts

box.sql.execute("DROP VIEW v1;")
box.sql.execute("DROP TABLE t1;")
