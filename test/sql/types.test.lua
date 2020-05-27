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
box.execute("CREATE TABLE t1 (id TEXT PRIMARY KEY, a NUMBER, b INT, c TEXT, d SCALAR);")
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

--
-- gh-4103: If resulting value of arithmetic operations is
-- integers, then make sure its type also integer (not number).
--
box.execute('SELECT 1 + 1;')
box.execute('SELECT 1 + 1.1;')
box.execute('SELECT \'9223372036854\' + 1;')

-- Fix BOOLEAN bindings.
box.execute('SELECT ?', {true})

-- gh-4187: make sure that value passsed to the iterator has
-- the same type as indexed fields.
--
box.execute("CREATE TABLE tboolean (s1 BOOLEAN PRIMARY KEY);")
box.execute("INSERT INTO tboolean VALUES (TRUE);")
box.execute("SELECT * FROM tboolean WHERE s1 = x'44';")
box.execute("SELECT * FROM tboolean WHERE s1 = 'abc';")
box.execute("SELECT * FROM tboolean WHERE s1 = 1;")
box.execute("SELECT * FROM tboolean WHERE s1 = 1.123;")

box.space.TBOOLEAN:drop()

box.execute("CREATE TABLE t1(id INT PRIMARY KEY, a INT UNIQUE);")
box.execute("INSERT INTO t1 VALUES (1, 1);")
box.execute("SELECT a FROM t1 WHERE a IN (1.1, 2.1);")
box.execute("SELECT a FROM t1 WHERE a = 1.1;")
box.execute("SELECT a FROM t1 WHERE a = 1.0;")
box.execute("SELECT a FROM t1 WHERE a > 1.1;")
box.execute("SELECT a FROM t1 WHERE a < 1.1;")

box.space.T1:drop()

box.execute("CREATE TABLE t1(id INT PRIMARY KEY, a INT, b INT);")
box.execute("CREATE INDEX i1 ON t1(a, b);")
box.execute("INSERT INTO t1 VALUES (1, 1, 1);")
box.execute("SELECT a FROM t1 WHERE a = 1.0 AND b > 0.5;")
box.execute("SELECT a FROM t1 WHERE a = 1.5 AND b IS NULL;")
box.execute("SELECT a FROM t1 WHERE a IS NULL AND b IS NULL;")

box.space.T1:drop()

format = {}
format[1] = { name = 'ID', type = 'unsigned' }
format[2] = { name = 'A', type = 'unsigned' }
s = box.schema.create_space('T1', { format = format })
_ = s:create_index('pk')
_ = s:create_index('sk', { parts = { 'A' } })
s:insert({ 1, 1 })
box.execute("SELECT a FROM t1 WHERE a IN (1.1, 2.1);")

s:drop()

-- gh-3810: range of integer is extended up to 2^64 - 1.
--
box.execute("SELECT 18446744073709551615 > 18446744073709551614;")
box.execute("SELECT 18446744073709551615 > -9223372036854775808;")
box.execute("SELECT -1 < 18446744073709551615;")
box.execute("SELECT 1.5 < 18446744073709551615")
box.execute("SELECT 1.5 > 18446744073709551615")
box.execute("SELECT 18446744073709551615 > 1.5")
box.execute("SELECT 18446744073709551615 < 1.5")
box.execute("SELECT 18446744073709551615 = 18446744073709551615;")
box.execute("SELECT 18446744073709551615 > -9223372036854775808;")
box.execute("SELECT 18446744073709551615 < -9223372036854775808;")
box.execute("SELECT -1 < 18446744073709551615;")
box.execute("SELECT -1 > 18446744073709551615;")
box.execute("SELECT 18446744073709551610 - 18446744073709551615;")
box.execute("SELECT 18446744073709551615 = null;")
box.execute("SELECT 18446744073709551615 = 18446744073709551615.0;")
box.execute("SELECT 18446744073709551615.0 > 18446744073709551615")
box.execute("SELECT 18446744073709551615 IN ('18446744073709551615', 18446744073709551615.0)")
box.execute("SELECT 1 LIMIT 18446744073709551615;")
box.execute("SELECT 1 LIMIT 1 OFFSET 18446744073709551614;")
box.execute("SELECT CAST('18446744073' || '709551616' AS INTEGER);")
box.execute("SELECT CAST('18446744073' || '709551615' AS INTEGER);")
box.execute("SELECT 18446744073709551610 + 5;")
box.execute("SELECT 18446744073709551615 * 1;")
box.execute("SELECT 1 / 18446744073709551615;")
box.execute("SELECT 18446744073709551615 / 18446744073709551615;")
box.execute("SELECT 18446744073709551615 / -9223372036854775808;")
box.execute("SELECT 0 - 18446744073709551610;")
box.execute("CREATE TABLE t (id INT PRIMARY KEY, i INT);")
box.execute("INSERT INTO t VALUES (1, 18446744073709551615);")
box.execute("INSERT INTO t VALUES (2, 18446744073709551614);")
box.execute("INSERT INTO t VALUES (3, 18446744073709551613)")
box.execute("SELECT i FROM t;")
box.execute("SELECT i FROM t WHERE i = 18446744073709551615;")
box.execute("SELECT i FROM t WHERE i BETWEEN 18446744073709551613 AND 18446744073709551615;")
box.execute("SELECT i FROM t ORDER BY i;")
box.execute("SELECT i FROM t ORDER BY -i;")
box.execute("SELECT i FROM t ORDER BY i LIMIT 1;")
-- Test that built-in functions are capable of handling unsigneds.
--
box.execute("DELETE FROM t WHERE i > 18446744073709551613;")
box.execute("INSERT INTO t VALUES (1, 1);")
box.execute("INSERT INTO t VALUES (2, -1);")
box.execute("SELECT sum(i) FROM t;")
box.execute("SELECT avg(i) FROM t;")
box.execute("SELECT total(i) FROM t;")
box.execute("SELECT min(i) FROM t;")
box.execute("SELECT max(i) FROM t;")
box.execute("SELECT count(i) FROM t;")
box.execute("SELECT group_concat(i) FROM t;")

box.execute("DELETE FROM t WHERE i < 18446744073709551613;")
box.execute("SELECT lower(i) FROM t;")
box.execute("SELECT upper(i) FROM t;")
box.execute("SELECT abs(i) FROM t;")
box.execute("SELECT typeof(i) FROM t;")
box.execute("SELECT quote(i) FROM t;")
box.execute("SELECT LEAST(-1, i) FROM t;")
box.execute("SELECT quote(i) FROM t;")

box.execute("CREATE INDEX i ON t(i);")
box.execute("SELECT i FROM t WHERE i = 18446744073709551613;")
box.execute("SELECT i FROM t WHERE i >= 18446744073709551613 ORDER BY i;")

box.execute("UPDATE t SET i = 18446744073709551615 WHERE i = 18446744073709551613;")
box.execute("SELECT i FROM t;")

-- Test constraints functionality.
--
box.execute("CREATE TABLE parent (id INT PRIMARY KEY, a INT UNIQUE);")
box.execute("INSERT INTO parent VALUES (1, 18446744073709551613);")
box.space.T:truncate()
box.execute("ALTER TABLE t ADD CONSTRAINT fk1 FOREIGN KEY (i) REFERENCES parent (a);")
box.execute("INSERT INTO t VALUES (1, 18446744073709551615);")
box.execute("INSERT INTO parent VALUES (2, 18446744073709551615);")
box.execute("INSERT INTO t VALUES (1, 18446744073709551615);")
box.execute("ALTER TABLE t DROP CONSTRAINT fk1;")
box.space.PARENT:drop()
box.space.T:drop()

box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a INT CHECK (a > 18446744073709551612));")
box.execute("INSERT INTO t1 VALUES (1, 18446744073709551611);")
box.execute("INSERT INTO t1 VALUES (1, -1);")
box.space.T1:drop()

box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a INT DEFAULT 18446744073709551615);")
box.execute("INSERT INTO t1 (id) VALUES (1);")
box.space.T1:select()
box.space.T1:drop()

-- Test that autoincrement accepts only max 2^63 - 1 .
--
box.execute("CREATE TABLE t1 (id INT PRIMARY KEY AUTOINCREMENT);")
box.execute("INSERT INTO t1 VALUES (18446744073709551615);")
box.execute("INSERT INTO t1 VALUES (NULL);")
box.space.T1:drop()

-- Test CAST facilities.
--
box.execute("SELECT CAST(18446744073709551615 AS NUMBER);")
box.execute("SELECT CAST(18446744073709551615 AS TEXT);")
box.execute("SELECT CAST(18446744073709551615 AS SCALAR);")
box.execute("SELECT CAST(18446744073709551615 AS BOOLEAN);")
box.execute("SELECT CAST('18446744073709551615' AS INTEGER);")

-- gh-4015: introduce unsigned type in SQL.
--
box.execute("CREATE TABLE t1 (id UNSIGNED PRIMARY KEY);")
box.execute("INSERT INTO t1 VALUES (0), (1), (2);")
box.execute("INSERT INTO t1 VALUES (-3);")
box.execute("SELECT id FROM t1;")

box.execute("SELECT CAST(123 AS UNSIGNED);")
box.execute("SELECT CAST(-123 AS UNSIGNED);")
box.execute("SELECT CAST(1.5 AS UNSIGNED);")
box.execute("SELECT CAST(-1.5 AS UNSIGNED);")
box.execute("SELECT CAST(true AS UNSIGNED);")
box.execute("SELECT CAST('123' AS UNSIGNED);")
box.execute("SELECT CAST('-123' AS UNSIGNED);")

box.space.T1:drop()

-- Check that STRING is a valid alias to TEXT type.
--
box.execute("CREATE TABLE t (id STRING PRIMARY KEY);")
box.space.T:format()[1].type
box.space.T:drop()

-- Make sure that CASE-THEN statement return type is SCALAR in
-- case two THEN clauses feature different types.
--
box.execute("SELECT CASE 1 WHEN 1 THEN x'0000000000' WHEN 2 THEN 'str' END")
box.execute("SELECT CASE 1 WHEN 1 THEN 666 WHEN 2 THEN 123 END")
box.execute("SELECT CASE 1 WHEN 1 THEN 666 WHEN 2 THEN 123 ELSE 321 END")
box.execute("SELECT CASE 1 WHEN 1 THEN 666 WHEN 2 THEN 123 ELSE 'asd' END")
box.execute("SELECT CASE 'a' WHEN 'a' THEN 1 WHEN 'b' THEN 2 WHEN 'c' THEN 3 WHEN 'd' THEN 4 WHEN 'e' THEN 5 WHEN 'f' THEN 6 END;")
box.execute("SELECT CASE 'a' WHEN 'a' THEN 1 WHEN 'b' THEN 2 WHEN 'c' THEN 3 WHEN 'd' THEN 4 WHEN 'e' THEN 5 WHEN 'f' THEN 'asd' END;")
box.execute("SELECT CASE 'a' WHEN 'a' THEN 1 WHEN 'b' THEN 2 WHEN 'c' THEN 3 WHEN 'd' THEN 4 WHEN 'e' THEN 5 WHEN 'f' THEN 6 ELSE 'asd' END;")
box.execute("SELECT CASE 'a' WHEN 'a' THEN 1 WHEN 'b' THEN 2 WHEN 'c' THEN 3 WHEN 'd' THEN 4 WHEN 'e' THEN 5 WHEN 'f' THEN 6 ELSE 7 END;")

-- Test basic capabilities of VARBINARY type.
--
box.execute("CREATE TABLE t (id INT PRIMARY KEY, v VARBINARY);")
box.execute("INSERT INTO t VALUES(1, 1);")
box.execute("INSERT INTO t VALUES(1, 1.123);")
box.execute("INSERT INTO t VALUES(1, true);")
box.execute("INSERT INTO t VALUES(1, 'asd');")
box.execute("INSERT INTO t VALUES(1, x'616263');")
box.execute("SELECT * FROM t WHERE v = 1")
box.execute("SELECT * FROM t WHERE v = 1.123")
box.execute("SELECT * FROM t WHERE v = 'str'")
box.execute("SELECT * FROM t WHERE v = x'616263'")

box.execute("SELECT sum(v) FROM t;")
box.execute("SELECT avg(v) FROM t;")
box.execute("SELECT total(v) FROM t;")
box.execute("SELECT min(v) FROM t;")
box.execute("SELECT max(v) FROM t;")
box.execute("SELECT count(v) FROM t;")
box.execute("SELECT group_concat(v) FROM t;")

box.execute("SELECT lower(v) FROM t;")
box.execute("SELECT upper(v) FROM t;")
box.execute("SELECT abs(v) FROM t;")
box.execute("SELECT typeof(v) FROM t;")
box.execute("SELECT quote(v) FROM t;")
box.execute("SELECT LEAST(v, x'') FROM t;")

box.execute("CREATE INDEX iv ON t(v);")
box.execute("SELECT v FROM t WHERE v = x'616263';")
box.execute("SELECT v FROM t ORDER BY v;")

box.execute("UPDATE t SET v = x'636261' WHERE v = x'616263';")
box.execute("SELECT v FROM t;")

box.execute("CREATE TABLE parent (id INT PRIMARY KEY, a VARBINARY UNIQUE);")
box.space.T:truncate()
box.execute("ALTER TABLE t ADD CONSTRAINT fk1 FOREIGN KEY (v) REFERENCES parent (a);")
box.execute("INSERT INTO t VALUES (1, x'616263');")
box.execute("INSERT INTO parent VALUES (1, x'616263');")
box.execute("INSERT INTO t VALUES (1, x'616263');")
box.execute("ALTER TABLE t DROP CONSTRAINT fk1;")
box.space.PARENT:drop()
box.space.T:drop()

box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a VARBINARY CHECK (a = x'616263'));")
box.execute("INSERT INTO t1 VALUES (1, x'006162');")
box.execute("INSERT INTO t1 VALUES (1, x'616263');")
box.space.T1:drop()

box.execute("CREATE TABLE t1 (id INT PRIMARY KEY, a VARBINARY DEFAULT x'616263');")
box.execute("INSERT INTO t1 (id) VALUES (1);")
box.space.T1:select()
box.space.T1:drop()

box.execute("SELECT CAST(1 AS VARBINARY);")
box.execute("SELECT CAST(1.123 AS VARBINARY);")
box.execute("SELECT CAST(true AS VARBINARY);")
box.execute("SELECT CAST('asd' AS VARBINARY);")
box.execute("SELECT CAST(x'' AS VARBINARY);")

-- gh-4148: make sure that typeof() returns origin type of column
-- even if value is null.
--
box.execute("CREATE TABLE t (id INT PRIMARY KEY, a INT, s SCALAR);")
box.execute("INSERT INTO t VALUES (1, 1, 1), (2, NULL, NULL);")
box.execute("SELECT typeof(a), typeof(s) FROM t;")

box.execute('CREATE TABLE t1 (id INTEGER PRIMARY KEY, a INTEGER, b INTEGER)')
box.execute('INSERT INTO t1 VALUES (1, NULL, NULL);')
box.execute('SELECT typeof(a & b) FROM t1;')
box.execute('SELECT typeof(a), typeof(b), typeof(a & b) FROM t1')

box.execute("SELECT typeof(CAST(0 AS UNSIGNED));")

box.space.T:drop()
box.space.T1:drop()

--
-- gh-4189: make sure that update doesn't throw an error if format
-- of table features map/array field types.
--
format = {}
format[1] = {type = 'integer', name = 'I'}
format[2] = {type = 'boolean', name = 'B'}
format[3] = {type = 'array', name = 'F1'}
format[4] = {type = 'map', name = 'F2'}
format[5] = {type = 'any', name = 'F3'}
s = box.schema.space.create('T', {format = format})
ii = s:create_index('ii')
s:insert({1, true, {1, 2}, {a = 3}, 'asd'})
box.execute('UPDATE t SET b = false WHERE i = 1;')
s:select()
s:drop()

--
-- Make sure that the array/map conversion to scalar error is
-- displayed correctly.
--
box.execute('CREATE TABLE t1(i INT PRIMARY KEY AUTOINCREMENT, a SCALAR);')
format = {}
format[1] = {type = 'integer', name = 'I'}
format[2] = {type = 'array', name = 'A'}
s = box.schema.space.create('T2', {format=format})
i = s:create_index('ii')
s:insert({1, {1,2,3}})
box.execute('INSERT INTO t1(a) SELECT a FROM t2;')
s:replace({1, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30}})
box.execute('INSERT INTO t1(a) SELECT a FROM t2;')
--
-- Make sure that the error will be displayed correctly even if
-- the value is too long.
--
long_array = {}
for i = 1,120 do long_array[i] = i end
s:replace({1, long_array})
box.execute('INSERT INTO t1(a) SELECT a FROM t2;')
s:drop()
format[2].type = 'map'
s = box.schema.space.create('T2', {format=format})
i = s:create_index('ii')
s:insert({1, {b = 1}})
box.execute('INSERT INTO t1(a) SELECT a FROM t2;')
s:drop()
box.execute('DROP TABLE t1;')

-- gh-3812: Make sure DOUBLE type works correctly.
box.execute("SELECT 1.0;")
box.execute("SELECT .01;")

box.execute("SELECT CAST(1 AS DOUBLE);")
box.execute("SELECT CAST(1.123 AS DOUBLE);")
box.execute("SELECT CAST(true AS DOUBLE);")
box.execute("SELECT CAST('asd' AS DOUBLE);")
box.execute("SELECT CAST('1' AS DOUBLE);")
box.execute("SELECT CAST('1.123' AS DOUBLE);")
box.execute("SELECT CAST(x'' AS DOUBLE);")
box.execute("SELECT CAST(x'35' AS DOUBLE);")
box.execute("SELECT CAST(CAST(x'35' AS STRING) AS DOUBLE);")

box.execute('CREATE TABLE t (i INT PRIMARY KEY AUTOINCREMENT, d DOUBLE);')
box.execute('INSERT INTO t(d) VALUES (10), (-2.0), (3.3), (18000000000000000000);')
box.execute('SELECT * FROM t;')
box.execute('SELECT d / 100 FROM t;')
box.execute('SELECT * from t WHERE d < 15;')
box.execute('SELECT * from t WHERE d = 3.3;')

box.execute("SELECT sum(d) FROM t;")
box.execute("SELECT avg(d) FROM t;")
box.execute("SELECT total(d) FROM t;")
box.execute("SELECT min(d) FROM t;")
box.execute("SELECT max(d) FROM t;")
box.execute("SELECT count(d) FROM t;")
box.execute("SELECT group_concat(d) FROM t;")

box.execute("SELECT lower(d) FROM t;")
box.execute("SELECT upper(d) FROM t;")
box.execute("SELECT abs(d) FROM t;")
box.execute("SELECT typeof(d) FROM t;")
box.execute("SELECT quote(d) FROM t;")
box.execute("SELECT LEAST(d, 0) FROM t;")

box.execute("CREATE INDEX dd ON t(d);")
box.execute("SELECT d FROM t WHERE d < 0;")
box.execute("SELECT d FROM t ORDER BY d;")

box.execute("UPDATE t SET d = 1 WHERE d = 10;")
box.execute("SELECT d FROM t;")
box.execute("DROP TABLE t;")

box.execute("CREATE TABLE t1 (d DOUBLE PRIMARY KEY);")
box.execute("INSERT INTO t1 VALUES (1), (2.2), (3.5);")
box.execute("INSERT INTO t1 VALUES (1);")

box.execute("CREATE TABLE t2 (i INT PRIMARY KEY, d DOUBLE REFERENCES t1);")
box.execute("INSERT INTO t2 VALUES (1,1), (2,2.2), (100, 3.5), (4, 1);")
box.execute("INSERT INTO t2 VALUES (5,10);")
box.execute("DROP TABLE t2;")
box.execute("DROP TABLE t1;")

box.execute("CREATE TABLE t3 (i INT PRIMARY KEY, d DOUBLE CHECK (d < 10));")
box.execute("INSERT INTO t3 VALUES (1, 1);")
box.execute("INSERT INTO t3 VALUES (2, 9.999999);")
box.execute("INSERT INTO t3 VALUES (3, 10.0000001);")
box.execute("SELECT * FROM t3;")
box.execute("DROP TABLE t3;")

box.execute("CREATE TABLE t4 (i INT PRIMARY KEY, d DOUBLE DEFAULT 1.2345);")
box.execute("INSERT INTO t4(i) VALUES (1);")
box.execute("SELECT * FROM t4;")
box.execute("DROP TABLE t4;")

-- Make sure the typeof() function works correctly with DOUBLE.
box.execute("SELECT 1.0, typeof(1.0);")
box.execute("SELECT CAST(2 AS DOUBLE), typeof(CAST(2 AS DOUBLE));")
box.execute("SELECT 3e3, typeof(3e3);")

box.execute("CREATE TABLE t5 (d DOUBLE PRIMARY KEY);")
box.execute("INSERT INTO t5 VALUES (4), (5.5), (6e6);")
box.execute("SELECT d, TYPEOF(d) FROM t5;")

-- gh-4728: make sure that given query doesn't result in
-- assertion fault.
--
s = box.schema.space.create('s')
_ = s:create_index('pk')
s:format({ \
    [1] = {name = 'id', type = 'unsigned'}, \
    [2] = {name = 'v', type = 'string', is_nullable = true}, \
})

box.execute([[SELECT * FROM "s" WHERE "id" = ?;]])
s:drop()

--
-- gh-3809: Make sure there are no implicit casts during
-- assignment, except for the implicit cast between numeric
-- values.
--

-- Check INSERT.
box.execute([[CREATE TABLE ti (a INT PRIMARY KEY AUTOINCREMENT, i INTEGER);]])
box.execute([[CREATE TABLE td (a INT PRIMARY KEY AUTOINCREMENT, d DOUBLE);]])
box.execute([[CREATE TABLE tb (a INT PRIMARY KEY AUTOINCREMENT, b BOOLEAN);]])
box.execute([[CREATE TABLE tt (a INT PRIMARY KEY AUTOINCREMENT, t TEXT);]])
box.execute([[CREATE TABLE tv (a INT PRIMARY KEY AUTOINCREMENT, v VARBINARY);]])
box.execute([[CREATE TABLE ts (a INT PRIMARY KEY AUTOINCREMENT, s SCALAR);]])

box.execute([[INSERT INTO ti(i) VALUES (NULL);]])
box.execute([[INSERT INTO ti(i) VALUES (11);]])
box.execute([[INSERT INTO ti(i) VALUES (100000000000000000000000000000000.1);]])
box.execute([[INSERT INTO ti(i) VALUES (33.0);]])
box.execute([[INSERT INTO ti(i) VALUES (true);]])
box.execute([[INSERT INTO ti(i) VALUES ('33');]])
box.execute([[INSERT INTO ti(i) VALUES (X'3434');]])
box.execute([[SELECT * FROM ti;]])

box.execute([[INSERT INTO td(d) VALUES (NULL);]])
box.execute([[INSERT INTO td(d) VALUES (11);]])
box.execute([[INSERT INTO td(d) VALUES (100000000000000001);;]])
box.execute([[INSERT INTO td(d) VALUES (22.2);]])
box.execute([[INSERT INTO td(d) VALUES (true);]])
box.execute([[INSERT INTO td(d) VALUES ('33');]])
box.execute([[INSERT INTO td(d) VALUES (X'3434');]])
box.execute([[SELECT * FROM td;]])

box.execute([[INSERT INTO tb(b) VALUES (NULL);]])
box.execute([[INSERT INTO tb(b) VALUES (11);]])
box.execute([[INSERT INTO tb(b) VALUES (22.2);]])
box.execute([[INSERT INTO tb(b) VALUES (true);]])
box.execute([[INSERT INTO tb(b) VALUES ('33');]])
box.execute([[INSERT INTO tb(b) VALUES (X'3434');]])
box.execute([[SELECT * FROM tb;]])

box.execute([[INSERT INTO tt(t) VALUES (NULL);]])
box.execute([[INSERT INTO tt(t) VALUES (11);]])
box.execute([[INSERT INTO tt(t) VALUES (22.2);]])
box.execute([[INSERT INTO tt(t) VALUES (true);]])
box.execute([[INSERT INTO tt(t) VALUES ('33');]])
box.execute([[INSERT INTO tt(t) VALUES (X'3434');]])
box.execute([[SELECT * FROM tt;]])

box.execute([[INSERT INTO tv(v) VALUES (NULL);]])
box.execute([[INSERT INTO tv(v) VALUES (11);]])
box.execute([[INSERT INTO tv(v) VALUES (22.2);]])
box.execute([[INSERT INTO tv(v) VALUES (true);]])
box.execute([[INSERT INTO tv(v) VALUES ('33');]])
box.execute([[INSERT INTO tv(v) VALUES (X'3434');]])
box.execute([[SELECT * FROM tv;]])

box.execute([[INSERT INTO ts(s) VALUES (NULL);]])
box.execute([[INSERT INTO ts(s) VALUES (11);]])
box.execute([[INSERT INTO ts(s) VALUES (22.2);]])
box.execute([[INSERT INTO ts(s) VALUES (true);]])
box.execute([[INSERT INTO ts(s) VALUES ('33');]])
box.execute([[INSERT INTO ts(s) VALUES (X'3434');]])
box.execute([[SELECT * FROM ts;]])

-- Check for UPDATE.
box.execute([[DELETE FROM ti;]])
box.execute([[DELETE FROM td;]])
box.execute([[DELETE FROM tb;]])
box.execute([[DELETE FROM tt;]])
box.execute([[DELETE FROM tv;]])
box.execute([[DELETE FROM ts;]])
box.execute([[INSERT INTO ti VALUES(1, NULL);]])
box.execute([[INSERT INTO td VALUES(1, NULL);]])
box.execute([[INSERT INTO tb VALUES(1, NULL);]])
box.execute([[INSERT INTO tt VALUES(1, NULL);]])
box.execute([[INSERT INTO tv VALUES(1, NULL);]])
box.execute([[INSERT INTO ts VALUES(1, NULL);]])
box.execute([[SELECT * FROM ti, td, tb, tt, tv, ts;]])

box.execute([[UPDATE ti SET i = NULL WHERE a = 1;]])
box.execute([[UPDATE ti SET i = 11 WHERE a = 1;]])
box.execute([[UPDATE ti SET i = 100000000000000000000000000000000.1 WHERE a = 1;]])
box.execute([[UPDATE ti SET i = 33.0 WHERE a = 1;]])
box.execute([[UPDATE ti SET i = true WHERE a = 1;]])
box.execute([[UPDATE ti SET i = '33' WHERE a = 1;]])
box.execute([[UPDATE ti SET i = X'3434' WHERE a = 1;]])
box.execute([[SELECT * FROM ti;]])

box.execute([[UPDATE td SET d = NULL WHERE a = 1;]])
box.execute([[UPDATE td SET d = 11 WHERE a = 1;]])
box.execute([[UPDATE td SET d = 100000000000000001 WHERE a = 1;]])
box.execute([[UPDATE td SET d = 22.2 WHERE a = 1;]])
box.execute([[UPDATE td SET d = true WHERE a = 1;]])
box.execute([[UPDATE td SET d = '33' WHERE a = 1;]])
box.execute([[UPDATE td SET d = X'3434' WHERE a = 1;]])
box.execute([[SELECT * FROM td;]])

box.execute([[UPDATE tb SET b = NULL WHERE a = 1;]])
box.execute([[UPDATE tb SET b = 11 WHERE a = 1;]])
box.execute([[UPDATE tb SET b = 22.2 WHERE a = 1;]])
box.execute([[UPDATE tb SET b = true WHERE a = 1;]])
box.execute([[UPDATE tb SET b = '33' WHERE a = 1;]])
box.execute([[UPDATE tb SET b = X'3434' WHERE a = 1;]])
box.execute([[SELECT * FROM tb;]])

box.execute([[UPDATE tt SET t = NULL WHERE a = 1;]])
box.execute([[UPDATE tt SET t = 11 WHERE a = 1;]])
box.execute([[UPDATE tt SET t = 22.2 WHERE a = 1;]])
box.execute([[UPDATE tt SET t = true WHERE a = 1;]])
box.execute([[UPDATE tt SET t = '33' WHERE a = 1;]])
box.execute([[UPDATE tt SET t = X'3434' WHERE a = 1;]])
box.execute([[SELECT * FROM tt;]])

box.execute([[UPDATE tv SET v = NULL WHERE a = 1;]])
box.execute([[UPDATE tv SET v = 11 WHERE a = 1;]])
box.execute([[UPDATE tv SET v = 22.2 WHERE a = 1;]])
box.execute([[UPDATE tv SET v = true WHERE a = 1;]])
box.execute([[UPDATE tv SET v = '33' WHERE a = 1;]])
box.execute([[UPDATE tv SET v = X'3434' WHERE a = 1;]])
box.execute([[SELECT * FROM tv;]])

box.execute([[UPDATE ts SET s = NULL WHERE a = 1;]])
box.execute([[UPDATE ts SET s = 11 WHERE a = 1;]])
box.execute([[UPDATE ts SET s = 22.2 WHERE a = 1;]])
box.execute([[UPDATE ts SET s = true WHERE a = 1;]])
box.execute([[UPDATE ts SET s = '33' WHERE a = 1;]])
box.execute([[UPDATE ts SET s = X'3434' WHERE a = 1;]])
box.execute([[SELECT * FROM ts;]])

box.execute([[DROP TABLE ti;]])
box.execute([[DROP TABLE td;]])
box.execute([[DROP TABLE tb;]])
box.execute([[DROP TABLE tt;]])
box.execute([[DROP TABLE tv;]])
box.execute([[DROP TABLE ts;]])
