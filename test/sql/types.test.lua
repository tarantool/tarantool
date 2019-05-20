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

-- gh-4229: allow explicit cast from string to integer for string
-- values containing quoted floating point literals.
--
box.execute("SELECT CAST('1.123' AS INTEGER);")
box.execute("CREATE TABLE t1 (f TEXT PRIMARY KEY);")
box.execute("INSERT INTO t1 VALUES('0.0'), ('1.5'), ('3.9312453');")
box.execute("SELECT CAST(f AS INTEGER) FROM t1;")
box.space.T1:drop()

-- Test basic capabilities of boolean type.
--
box.execute("SELECT true;")
box.execute("SELECT false;")
box.execute("SELECT unknown;")
box.execute("SELECT true = false;")
box.execute("SELECT true = true;")
box.execute("SELECT true > false;")
box.execute("SELECT true < false;")
box.execute("SELECT null = true;")
box.execute("SELECT unknown = true;")
box.execute("SELECT 1 = true;")
box.execute("SELECT 'abc' = true;")
box.execute("SELECT 1.123 > true;")
box.execute("SELECT true IN (1, 'abc', true)")
box.execute("SELECT true IN (1, 'abc', false)")
box.execute("SELECT 1 LIMIT true;")
box.execute("SELECT 1 LIMIT 1 OFFSET true;")
box.execute("SELECT 'abc' || true;")

-- Boolean can take part in arithmetic operations.
--
box.execute("SELECT true + false;")
box.execute("SELECT true * 1;")
box.execute("SELECT false / 0;")
box.execute("SELECT not true;")
box.execute("SELECT ~true;")
box.execute("SELECT -true;")
box.execute("SELECT true << 1;")
box.execute("SELECT true | 1;")
box.execute("SELECT true and false;")
box.execute("SELECT true or unknown;")

box.execute("CREATE TABLE t (id INT PRIMARY KEY, b BOOLEAN);")
box.execute("INSERT INTO t VALUES (1, true);")
box.execute("INSERT INTO t VALUES (2, false);")
box.execute("INSERT INTO t VALUES (3, unknown)")
box.execute("SELECT b FROM t;")
box.execute("SELECT b FROM t WHERE b = false;")
box.execute("SELECT b FROM t WHERE b IS NULL;")
box.execute("SELECT b FROM t WHERE b IN (false, 1, 'abc')")
box.execute("SELECT b FROM t WHERE b BETWEEN false AND true;")
box.execute("SELECT b FROM t WHERE b BETWEEN true AND false;")
box.execute("SELECT b FROM t ORDER BY b;")
box.execute("SELECT b FROM t ORDER BY +b;")
box.execute("SELECT b FROM t ORDER BY b LIMIT 1;")
box.execute("SELECT b FROM t GROUP BY b LIMIT 1;")
box.execute("SELECT b FROM t LIMIT true;")

-- Most of aggregates don't accept boolean arguments.
--
box.execute("SELECT sum(b) FROM t;")
box.execute("SELECT avg(b) FROM t;")
box.execute("SELECT total(b) FROM t;")
box.execute("SELECT min(b) FROM t;")
box.execute("SELECT max(b) FROM t;")
box.execute("SELECT count(b) FROM t;")
box.execute("SELECT group_concat(b) FROM t;")

-- Check other built-in functions.
--
box.execute("SELECT lower(b) FROM t;")
box.execute("SELECT upper(b) FROM t;")
box.execute("SELECT abs(b) FROM t;")
box.execute("SELECT typeof(b) FROM t;")
box.execute("SELECT quote(b) FROM t;")
box.execute("SELECT min(b, true) FROM t;")
box.execute("SELECT quote(b) FROM t;")

-- Test index search using boolean values.
--
box.execute("CREATE INDEX ib ON t(b);")
box.execute("SELECT b FROM t WHERE b = false;")
box.execute("SELECT b FROM t WHERE b OR unknown ORDER BY b;")

-- Test UPDATE on boolean field.
--
box.execute("UPDATE t SET b = true WHERE b = false;")
box.execute("SELECT b FROM t;")

-- Test constraints functionality.
--
box.execute("CREATE TABLE parent (id INT PRIMARY KEY, a BOOLEAN UNIQUE);")
box.space.T:truncate()
box.execute("ALTER TABLE t ADD CONSTRAINT fk1 FOREIGN KEY (b) REFERENCES parent (a);")
box.execute("INSERT INTO t VALUES (1, true);")
box.execute("INSERT INTO parent VALUES (1, true);")
box.execute("INSERT INTO t VALUES (1, true);")
box.execute("ALTER TABLE t DROP CONSTRAINT fk1;")
box.space.PARENT:drop()

box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a BOOLEAN CHECK (a = true));")
box.execute("INSERT INTO t1 VALUES (1, false);")
box.execute("INSERT INTO t1 VALUES (1, true);")
box.space.T1:drop()

box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a BOOLEAN DEFAULT true);")
box.execute("INSERT INTO t1 (id) VALUES (1);")
box.space.T1:select()
box.space.T1:drop()

-- Check that VIEW inherits boolean type.
--
box.execute("CREATE VIEW v AS SELECT b FROM t;")
box.space.V:format()[1]['type']
box.space.V:drop()

-- Test CAST facilities.
--
box.execute("SELECT CAST(true AS INTEGER);")
box.execute("SELECT CAST(true AS TEXT);")
box.execute("SELECT CAST(true AS FLOAT);")
box.execute("SELECT CAST(true AS SCALAR);")
box.execute("SELECT CAST(1 AS BOOLEAN);")
box.execute("SELECT CAST(1.123 AS BOOLEAN);")
box.execute("SELECT CAST(0.0 AS BOOLEAN);")
box.execute("SELECT CAST(0.00000001 AS BOOLEAN);")
box.execute("SELECT CAST('abc' AS BOOLEAN);")
box.execute("SELECT CAST('  TrUe' AS BOOLEAN);")
box.execute("SELECT CAST('  falsE    ' AS BOOLEAN);")
box.execute("SELECT CAST('  fals' AS BOOLEAN);")

box.execute("SELECT CAST(X'4D6564766564' AS BOOLEAN);")

-- Make sure that SCALAR can handle boolean values.
--
box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, s SCALAR);")
box.execute("INSERT INTO t1 SELECT * FROM t;")
box.execute("SELECT s FROM t1 WHERE s = true;")
box.execute("INSERT INTO t1 VALUES (3, 'abc'), (4, 12.5);")
box.execute("SELECT s FROM t1 WHERE s = true;")
box.execute("SELECT s FROM t1 WHERE s < true;")
box.execute("SELECT s FROM t1 WHERE s IN (true, 1, 'abcd')")

box.space.T:drop()
box.space.T1:drop()

--
-- gh-4103: If resulting value of arithmetic operations is
-- integers, then make sure its type also integer (not number).
--
box.execute('SELECT 1 + 1;')
box.execute('SELECT 1 + 1.1;')
box.execute('SELECT \'9223372036854\' + 1;')

-- Fix BOOLEAN bindings.
box.execute('SELECT ?', {true})
