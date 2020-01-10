remote = require('net.box')
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- gh-3010: COLLATE after LIMIT should throw an error

-- All of these tests should throw error "near "COLLATE": syntax error"
box.execute("SELECT 1 LIMIT 1 COLLATE BINARY;")
box.execute("SELECT 1 LIMIT 1 COLLATE BINARY OFFSET 1;")
box.execute("SELECT 1 LIMIT 1 OFFSET 1 COLLATE BINARY;")
box.execute("SELECT 1 LIMIT 1, 1 COLLATE BINARY;")
box.execute("SELECT 1 LIMIT 1 COLLATE BINARY, 1;")

-- gh-3052: upper/lower support only default locale
-- For tr-TR result depends on collation
box.execute([[CREATE TABLE tu (descriptor VARCHAR(50) PRIMARY KEY, letter VARCHAR(50))]]);
box.internal.collation.create('TURKISH', 'ICU', 'tr-TR', {strength='primary'});
box.execute([[INSERT INTO tu VALUES ('Latin Capital Letter I U+0049','I');]])
box.execute([[INSERT INTO tu VALUES ('Latin Small Letter I U+0069','i');]])
box.execute([[INSERT INTO tu VALUES ('Latin Capital Letter I With Dot Above U+0130','İ');]])
box.execute([[INSERT INTO tu VALUES ('Latin Small Letter Dotless I U+0131','ı');]])
-- Without collation
box.execute([[SELECT descriptor, upper(letter) AS upper,lower(letter) AS lower FROM tu;]])
-- With collation
box.execute([[SELECT descriptor, upper(letter COLLATE "TURKISH") AS upper,lower(letter COLLATE "TURKISH") AS lower FROM tu;]])
box.internal.collation.drop('TURKISH')

-- For de-DE result is actually the same
box.internal.collation.create('GERMAN', 'ICU', 'de-DE', {strength='primary'});
box.execute([[INSERT INTO tu VALUES ('German Small Letter Sharp S U+00DF','ß');]])
-- Without collation
box.execute([[SELECT descriptor, upper(letter), letter FROM tu where UPPER(letter) = 'SS';]])
-- With collation
box.execute([[SELECT descriptor, upper(letter COLLATE "GERMAN"), letter FROM tu where UPPER(letter COLLATE "GERMAN") = 'SS';]])
box.internal.collation.drop('GERMAN')
box.execute(([[DROP TABLE tu]]))

box.schema.user.grant('guest','read,write,execute', 'universe')
cn = remote.connect(box.cfg.listen)

cn:execute('select 1 limit ? collate not_exist', {1})

cn:close()

-- Explicitly set BINARY collation is predifined and has ID.
--
box.execute("CREATE TABLE t (id INT PRIMARY KEY, a TEXT, b TEXT COLLATE \"binary\");")
box.space.T:format()[2]['collation']
box.space.T:format()[3]['collation']
box.execute("DROP TABLE t;")

-- BINARY collation works in the same way as there is no collation
-- at all.
--
t = box.schema.create_space('test', {format = {{'id', 'unsigned'}, {'a', 'string', collation = 'binary'}}})
t:format()[2]['collation']
pk = t:create_index('primary', {parts = {1}})
i = t:create_index('secondary', {parts = {2, 'str', collation='binary'}})
t:insert({1, 'AsD'})
t:insert({2, 'ASD'})
t:insert({3, 'asd'})
i:select('asd')
i:select('ASD')
i:select('AsD')
t:drop()

-- Collation with id == 0 is "none". It used to unify interaction
-- with collation interface. It also can't be dropped.
--
box.space._collation:select{0}
box.space._collation:delete{0}

-- gh-3185: collations of LHS and RHS must be compatible.
--
box.execute("CREATE TABLE t (id INT PRIMARY KEY, a TEXT, b TEXT COLLATE \"binary\", c TEXT COLLATE \"unicode_ci\");")
box.execute("SELECT * FROM t WHERE a = b;")
box.execute("SELECT * FROM t WHERE a COLLATE \"binary\" = b;")
box.execute("SELECT * FROM t WHERE b = c;")
box.execute("SELECT * FROM t WHERE b COLLATE \"binary\" = c;")
box.execute("SELECT * FROM t WHERE a = c;")
box.execute("SELECT * FROM t WHERE a COLLATE \"binary\" = c COLLATE \"unicode\";")
-- Make sure that using function featuring variable arguemnts
-- length  and resulting collation which depends on arguments
-- is processed correctly.
--
box.execute("SELECT * FROM t WHERE a COLLATE \"binary\" = substr();")

-- Compound queries perform implicit comparisons between values.
-- Hence, rules for collations compatibilities are the same.
--
box.execute("SELECT 'abc' COLLATE \"binary\" UNION SELECT 'ABC' COLLATE \"unicode_ci\"")
box.execute("SELECT 'abc' COLLATE \"unicode_ci\" UNION SELECT 'ABC' COLLATE binary")
box.execute("SELECT c FROM t UNION SELECT b FROM t;")
box.execute("SELECT b FROM t UNION SELECT a FROM t;")
box.execute("SELECT a FROM t UNION SELECT c FROM t;")
box.execute("SELECT c COLLATE \"binary\" FROM t UNION SELECT a FROM t;")
box.execute("SELECT b COLLATE \"unicode\" FROM t UNION SELECT a FROM t;")

box.execute("DROP TABLE t;")
box.schema.user.revoke('guest', 'read,write,execute', 'universe')

-- gh-3857 "PRAGMA collation_list" invokes segmentation fault.
box.schema.user.create('tmp')
box.session.su('tmp')
-- gh-4713 "PRAGMA collation_list" is not accessible to all users
_, err = box.execute('pragma collation_list')
assert(err == nil)
box.session.su('admin')
box.schema.user.drop('tmp')

-- gh-3644 Foreign key update fails with "unicode_ci".
-- Check that foreign key update doesn't fail with "unicode_ci".
box.execute('CREATE TABLE t0 (s1 VARCHAR(5) COLLATE "unicode_ci" UNIQUE, id INT PRIMARY KEY AUTOINCREMENT);')
box.execute('CREATE TABLE t1 (s1 INT PRIMARY KEY, s0 VARCHAR(5) COLLATE "unicode_ci" REFERENCES t0(s1));')
box.execute("INSERT INTO t0(s1) VALUES ('a');")
box.execute("INSERT INTO t1 VALUES (1,'a');")
-- Should't fail.
box.execute("UPDATE t0 SET s1 = 'A';")
box.execute("SELECT s1 FROM t0;")
box.execute("SELECT * FROM t1;")
box.execute("DROP TABLE t1;")
box.execute("DROP TABLE t0;")
-- Check that foreign key update fails with default collation.
box.execute('CREATE TABLE t0 (s1 VARCHAR(5) UNIQUE, id INT PRIMARY KEY AUTOINCREMENT);')
box.execute('CREATE TABLE t1 (s1 INT PRIMARY KEY, s0 VARCHAR(5) REFERENCES t0(s1));')
box.execute("INSERT INTO t0(s1) VALUES ('a');")
box.execute("INSERT INTO t1 VALUES (1,'a');")
-- Should fail.
box.execute("UPDATE t0 SET s1 = 'A';")
box.execute("SELECT * FROM t1;")
box.execute("SELECT s1 FROM t0;")
box.execute("DROP TABLE t1;")
box.execute("DROP TABLE t0;")

-- gh-3937: result of concatination has derived collation.
--
box.execute("CREATE TABLE t4a(a TEXT COLLATE \"unicode\", b TEXT COLLATE \"unicode_ci\", c INT PRIMARY KEY);")
box.execute("INSERT INTO t4a VALUES('ABC','abc',1);")
box.execute("INSERT INTO t4a VALUES('ghi','ghi',3);")
-- Only LHS of concatenation has implicitly set collation.
-- Hence, no collation is used.
--
box.execute("SELECT c FROM t4a WHERE (a||'') = b;")
-- BINARY collation is forced for comparison operator as
-- a derivation from concatenation.
--
box.execute("SELECT c FROM t4a WHERE (a COLLATE \"binary\"||'') = b;")
-- Both operands of concatenation have explicit but different
-- collations.
--
box.execute("SELECT c FROM t4a WHERE (a COLLATE \"binary\"||'' COLLATE \"unicode_ci\") = b;")
box.execute("SELECT c FROM t4a WHERE (a COLLATE \"binary\"||'') = b COLLATE \"unicode\";")
-- No collation is used since LHS and RHS of concatenation
-- operator have different implicit collations.
--
box.execute("SELECT c FROM t4a WHERE (a||'')=(b||'');")
box.execute("SELECT c FROM t4a WHERE (a||b)=(b||a);")

box.execute("CREATE TABLE t4b(a TEXT COLLATE \"unicode_ci\", b TEXT COLLATE \"unicode_ci\", c INT PRIMARY KEY);")
box.execute("INSERT INTO t4b VALUES('BCD','bcd',1);")
box.execute("INSERT INTO t4b VALUES('ghi','ghi',3);")
-- Operands have the same implicit collation, so it is derived.
--
box.execute("SELECT c FROM t4a WHERE (a||b)=(b||a);")
-- Couple of other possible combinations.
--
box.execute("SELECT c FROM t4a WHERE (a||b COLLATE \"binary\")=(b||a);")
box.execute("SELECT c FROM t4a WHERE (a||b COLLATE \"binary\")=(b COLLATE \"unicode_ci\"||a);")

box.execute("INSERT INTO t4b VALUES('abc', 'xxx', 2);")
box.execute("INSERT INTO t4b VALUES('gHz', 'xxx', 4);")
-- Results are sorted with case-insensitive order.
-- Otherwise capital latters come first.
--
box.execute("SELECT a FROM t4b ORDER BY a COLLATE \"unicode_ci\" || ''")
box.execute("SELECT a FROM t4b ORDER BY a || b")

box.space.T4A:drop()
box.space.T4B:drop()

-- gh-3537 Duplicate key error for an index that is not unique
-- pk - default, sc - unicode_ci
box.execute('CREATE TABLE t3 (s1 VARCHAR(5) PRIMARY KEY);')
box.execute('CREATE INDEX i3 ON t3 (s1 collate "unicode_ci");')
box.execute("INSERT INTO t3 VALUES ('a');")
box.execute("INSERT INTO t3 VALUES ('A');")
box.execute("SELECT * FROM t3;")
box.execute("DROP TABLE t3;")

-- pk - binary, sc - unicode
box.execute('CREATE TABLE t3b (s1 VARCHAR(5) collate "binary" PRIMARY KEY);')
box.execute('CREATE INDEX i3b ON t3b (s1 collate "unicode");')
box.execute("INSERT INTO t3b VALUES ('a');")
box.execute("INSERT INTO t3b VALUES ('A');")
box.execute("SELECT * FROM t3b;")
box.execute("DROP TABLE t3b;")

-- pk - binary, sc - unicode (make dup)
box.execute('CREATE TABLE t3b (s1 VARCHAR(5) collate "binary" PRIMARY KEY);')
box.execute('CREATE INDEX i3b ON t3b (s1 collate "unicode");')
box.execute("INSERT INTO t3b VALUES ('a');")
box.execute("INSERT INTO t3b VALUES ('A');")
box.execute("INSERT INTO t3b VALUES ('a');")
box.execute("SELECT * FROM t3b;")
box.execute("DROP TABLE t3b;")

-- pk - unicode, sc - binary
box.execute('CREATE TABLE t3c (s1 VARCHAR(5) collate "unicode" PRIMARY KEY);')
box.execute('CREATE INDEX i3c ON t3c (s1 collate "binary");')
box.execute("INSERT INTO t3c VALUES ('a');")
box.execute("INSERT INTO t3c VALUES ('A');")
box.execute("SELECT * FROM t3c;")
box.execute("DROP TABLE t3c;")

-- pk - unicode, sc - binary (make dup)
box.execute('CREATE TABLE t3c (s1 VARCHAR(5) collate "unicode" PRIMARY KEY);')
box.execute('CREATE INDEX i3c ON t3c (s1 collate "binary");')
box.execute("INSERT INTO t3c VALUES ('a');")
box.execute("INSERT INTO t3c VALUES ('A');")
box.execute("INSERT INTO t3c VALUES ('a');")
box.execute("SELECT * FROM t3c;")
box.execute("DROP TABLE t3c;")

-- pk - binary, sc - unicode_ci
box.execute('CREATE TABLE t3d (s1 VARCHAR(5) collate "binary" PRIMARY KEY);')
box.execute('CREATE INDEX i3d ON t3d (s1 collate "unicode_ci");')
box.execute("INSERT INTO t3d VALUES ('a');")
box.execute("INSERT INTO t3d VALUES ('A');")
box.execute("SELECT * FROM t3d;")
box.execute("DROP TABLE t3d;")

-- pk - binary, sc - unicode_ci (make dup)
box.execute('CREATE TABLE t3d (s1 VARCHAR(5) collate "binary" PRIMARY KEY);')
box.execute('CREATE INDEX i3d ON t3d (s1 collate "unicode_ci");')
box.execute("INSERT INTO t3d VALUES ('a');")
box.execute("INSERT INTO t3d VALUES ('A');")
box.execute("INSERT INTO t3d VALUES ('a');")
box.execute("SELECT * FROM t3d;")
box.execute("DROP TABLE t3d;")

-- pk - unicode_ci, sc - binary (should fail)
box.execute('CREATE TABLE t3e (s1 VARCHAR(5) collate "unicode_ci" PRIMARY KEY);')
box.execute('CREATE INDEX i3e ON t3e (s1 collate "binary");')
box.execute("INSERT INTO t3e VALUES ('a');")
box.execute("INSERT INTO t3e VALUES ('A');")
box.execute("SELECT * FROM t3e;")
box.execute("DROP TABLE t3e;")

-- pk - unicode, sc - unicode_ci
box.execute('CREATE TABLE t3f (s1 VARCHAR(5) collate "unicode" PRIMARY KEY);')
box.execute('CREATE INDEX i3f ON t3f (s1 collate "unicode_ci");')
box.execute("INSERT INTO t3f VALUES ('a');")
box.execute("INSERT INTO t3f VALUES ('A');")
box.execute("SELECT * FROM t3f;")
box.execute("DROP TABLE t3f;")

-- pk - unicode, sc - unicode_ci (make dup)
box.execute('CREATE TABLE t3f (s1 VARCHAR(5) collate "unicode" PRIMARY KEY);')
box.execute('CREATE INDEX i3f ON t3f (s1 collate "unicode_ci");')
box.execute("INSERT INTO t3f VALUES ('a');")
box.execute("INSERT INTO t3f VALUES ('A');")
box.execute("INSERT INTO t3f VALUES ('a');")
box.execute("SELECT * FROM t3f;")
box.execute("DROP TABLE t3f;")

-- pk - unicode_ci, sc - unicode (should fail)
box.execute('CREATE TABLE t3g (s1 VARCHAR(5) collate "unicode_ci" PRIMARY KEY);')
box.execute('CREATE INDEX i3g ON t3g (s1 collate "unicode");')
box.execute("INSERT INTO t3g VALUES ('a');")
box.execute("INSERT INTO t3g VALUES ('A');")
box.execute("SELECT * FROM t3g;")
box.execute("DROP TABLE t3g;")

-- pk - default, sc - multipart
box.execute('CREATE TABLE qms1 (w VARCHAR(5) PRIMARY KEY, n VARCHAR(5), q VARCHAR(5), s INTEGER);')
box.execute('CREATE INDEX iqms1 ON qms1 (w collate "unicode_ci", n);')
box.execute("INSERT INTO qms1 VALUES ('www', 'nnn', 'qqq', 1);")
box.execute("INSERT INTO qms1 VALUES ('WWW', 'nnn', 'qqq', 2);")
box.execute("SELECT * FROM qms1;")
box.execute("DROP TABLE qms1;")

box.execute('CREATE TABLE qms2 (w VARCHAR(5) PRIMARY KEY, n VARCHAR(5), q VARCHAR(5), s INTEGER);')
box.execute('CREATE INDEX iqms2 ON qms2 (w collate "unicode", n);')
box.execute("INSERT INTO qms2 VALUES ('www', 'nnn', 'qqq', 1);")
box.execute("INSERT INTO qms2 VALUES ('WWW', 'nnn', 'qqq', 2);")
box.execute("SELECT * FROM qms2;")
box.execute("DROP TABLE qms2;")

-- pk - multipart, sc overlaps with pk
box.execute('CREATE TABLE qms3 (w VARCHAR(5), n VARCHAR(5), q VARCHAR(5), s INTEGER, CONSTRAINT pk_qms3 PRIMARY KEY(w, n, q));')
box.execute('CREATE INDEX iqms3 ON qms3 (w collate "unicode_ci", s);')
box.execute("INSERT INTO qms3 VALUES ('www', 'nnn', 'qqq', 1);")
box.execute("INSERT INTO qms3 VALUES ('WWW', 'nnn', 'qqq', 2);")
box.execute("SELECT * FROM qms3;")
box.execute("DROP TABLE qms3;")

box.execute('CREATE TABLE qms4 (w VARCHAR(5), n VARCHAR(5), q VARCHAR(5), s INTEGER, CONSTRAINT pk_qms4 PRIMARY KEY(w, n, q));')
box.execute('CREATE INDEX iqms4 ON qms4 (w collate "unicode", s);')
box.execute("INSERT INTO qms4 VALUES ('www', 'nnn', 'qqq', 1);")
box.execute("INSERT INTO qms4 VALUES ('WWW', 'nnn', 'qqq', 2);")
box.execute("SELECT * FROM qms4;")
box.execute("DROP TABLE qms4;")

-- gh-3932: make sure set build-in functions derive collation
-- from their arguments.
--
box.execute("CREATE TABLE jj (s1 INT PRIMARY KEY, s2 VARCHAR(3) COLLATE \"unicode_ci\");")
box.execute("INSERT INTO jj VALUES (1,'A'), (2,'a')")
box.execute("SELECT DISTINCT trim(s2) FROM jj;")
box.execute("INSERT INTO jj VALUES (3, 'aS'), (4, 'AS');")
box.execute("SELECT DISTINCT replace(s2, 'S', 's') FROM jj;")
box.execute("SELECT DISTINCT substr(s2, 1, 1) FROM jj;")
box.space.JJ:drop()

-- gh-3573: Strength in the _collation space
-- Collation without 'strength' option set now has explicit
-- 'strength' = 'tertiary'.
--
box.internal.collation.create('c', 'ICU', 'unicode')
box.space._collation.index.name:get({'c'})
box.internal.collation.drop('c')

--
-- gh-4007 Feature request for a new collation
--
-- Default unicode collation deals with russian letters
s = box.schema.space.create('t1')
s:format({{name='s1', type='string', collation = 'unicode'}})
idx = s:create_index('pk', {unique = true, type='tree', parts={{'s1', collation = 'unicode'}}})
s:insert{'Ё'}
s:insert{'Е'}
s:insert{'ё'}
s:insert{'е'}
-- all 4 letters are in the table
s:select{}
s:drop()

-- unicode_ci collation doesn't distinguish russian letters 'Е' and 'Ё'
s = box.schema.space.create('t1')
s:format({{name='s1', type='string', collation = 'unicode_ci'}})
idx = s:create_index('pk', {unique = true, type='tree', parts={{'s1', collation = 'unicode_ci'}}})
s:insert{'Ё'}
-- the following calls should fail
s:insert{'е'}
s:insert{'Е'}
s:insert{'ё'}
-- return single 'Ё'
s:select{}
s:drop()

