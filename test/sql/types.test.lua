env = require('test_run')
test_run = env.new()

-- gh-3018: typeless columns are prohibited.
--
box.execute("CREATE TABLE t1 (id PRIMARY KEY);")
box.execute("CREATE TABLE t1 (a, id INT PRIMARY KEY);")
box.execute("CREATE TABLE t1 (id PRIMARY KEY, a INT);")
box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a);")
box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a INT, b UNIQUE);")

-- gh-3104: real type is stored in space format.
--
box.execute("CREATE TABLE t1 (id TEXT PRIMARY KEY, a REAL, b INT, c TEXT, d SCALAR);")
box.space.T1:format()
box.execute("CREATE VIEW v1 AS SELECT b + a, b - a FROM t1;")
box.space.V1:format()

-- gh-2494: index's part also features correct declared type.
--
box.execute("CREATE INDEX i1 ON t1 (a);")
box.execute("CREATE INDEX i2 ON t1 (b);")
box.execute("CREATE INDEX i3 ON t1 (c);")
box.execute("CREATE INDEX i4 ON t1 (id, c, b, a, d);")
box.space.T1.index.I1.parts
box.space.T1.index.I2.parts
box.space.T1.index.I3.parts
box.space.T1.index.I4.parts

box.execute("DROP VIEW v1;")
box.execute("DROP TABLE t1;")

-- gh-3906: data of type BOOL is displayed as should
-- during SQL SELECT.
--
format = {{ name = 'ID', type = 'unsigned' }, { name = 'A', type = 'boolean' }}
sp = box.schema.space.create("TEST", { format = format } )
i = sp:create_index('primary', {parts = {1, 'unsigned' }})
sp:insert({1, true})
sp:insert({2, false})
box.execute("SELECT * FROM test")
sp:drop()

-- gh-3544: concatenation operator accepts only TEXT and BLOB.
--
box.execute("SELECT 'abc' || 1;")
box.execute("SELECT 'abc' || 1.123;")
box.execute("SELECT 1 || 'abc';")
box.execute("SELECT 1.123 || 'abc';")
box.execute("SELECt 'a' || 'b' || 1;")
-- What is more, they must be of the same type.
--
box.execute("SELECT 'abc' || randomblob(5);")
box.execute("SELECT randomblob(5) || 'x';")
-- Result of BLOBs concatenation must be BLOB.
--
box.execute("VALUES (TYPEOF(randomblob(5) || zeroblob(5)));")

-- gh-3954: LIKE accepts only arguments of type TEXT and NULLs.
--
box.execute("CREATE TABLE t1 (s SCALAR PRIMARY KEY);")
box.execute("INSERT INTO t1 VALUES (randomblob(5));")
box.execute("SELECT * FROM t1 WHERE s LIKE 'blob';")
box.execute("SELECT * FROM t1 WHERE 'blob' LIKE s;")
box.execute("SELECT * FROM t1 WHERE 'blob' LIKE x'0000';")
box.execute("SELECT s LIKE NULL FROM t1;")
box.execute("DELETE FROM t1;")
box.execute("INSERT INTO t1 VALUES (1);")
box.execute("SELECT * FROM t1 WHERE s LIKE 'int';")
box.execute("SELECT * FROM t1 WHERE 'int' LIKE 4;")
box.execute("SELECT NULL LIKE s FROM t1;")
box.space.T1:drop()
