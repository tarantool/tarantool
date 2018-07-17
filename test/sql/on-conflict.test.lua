test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')
--
-- Check that original SQLite ON CONFLICT clause is really
-- disabled.
--
box.sql.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT ABORT)")
box.sql.execute("CREATE TABLE q (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT FAIL)")
box.sql.execute("CREATE TABLE p (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT IGNORE)")
box.sql.execute("CREATE TABLE g (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT REPLACE)")
box.sql.execute("CREATE TABLE e (id INTEGER PRIMARY KEY ON CONFLICT REPLACE, v INTEGER)")
box.sql.execute("CREATE TABLE t1(a INT PRIMARY KEY ON CONFLICT REPLACE)")
box.sql.execute("CREATE TABLE t2(a INT PRIMARY KEY ON CONFLICT IGNORE)")

-- CHECK constraint is illegal with REPLACE option.
--
box.sql.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, a CHECK (a > 5) ON CONFLICT REPLACE);")

--
-- gh-3473: Primary key can't be declared with NULL.
--
box.sql.execute("CREATE TABLE te17 (s1 INT NULL PRIMARY KEY NOT NULL);")
box.sql.execute("CREATE TABLE te17 (s1 INT NULL PRIMARY KEY);")
box.sql.execute("CREATE TABLE test (a int PRIMARY KEY, b int NULL ON CONFLICT IGNORE);")
box.sql.execute("CREATE TABLE test (a int, b int NULL, c int, PRIMARY KEY(a, b, c))")

-- Several NOT NULL REPLACE constraints work
--
box.sql.execute("CREATE TABLE a (id INT PRIMARY KEY, a NOT NULL ON CONFLICT REPLACE DEFAULT 1, b NOT NULL ON CONFLICT REPLACE DEFAULT 2);")
box.sql.execute("INSERT INTO a VALUES(1, NULL, NULL);")
box.sql.execute("INSERT INTO a VALUES(2, NULL, NULL);")
box.sql.execute("SELECT * FROM a;")
box.sql.execute("DROP TABLE a;")

-- gh-3566: UPDATE OR IGNORE causes deletion of old entry.
--
box.sql.execute("CREATE TABLE tj (s1 INT PRIMARY KEY, s2 INT);")
box.sql.execute("INSERT INTO tj VALUES (1, 2), (2, 3);")
box.sql.execute("CREATE UNIQUE INDEX i ON tj (s2);")
box.sql.execute("UPDATE OR IGNORE tj SET s1 = s1 + 1;")
box.sql.execute("SELECT * FROM tj;")
box.sql.execute("UPDATE OR IGNORE tj SET s2 = s2 + 1;")
box.sql.execute("SELECT * FROM tj;")

-- gh-3565: INSERT OR REPLACE causes assertion fault.
--
box.sql.execute("DROP TABLE tj;")
box.sql.execute("CREATE TABLE tj (s1 INT PRIMARY KEY, s2 INT);")
box.sql.execute("INSERT INTO tj VALUES (1, 2),(2, 3);")
box.sql.execute("CREATE UNIQUE INDEX i ON tj (s2);")
box.sql.execute("REPLACE INTO tj VALUES (1, 3);")
box.sql.execute("SELECT * FROM tj;")
box.sql.execute("INSERT INTO tj VALUES (2, 4), (3, 5);")
box.sql.execute("UPDATE OR REPLACE tj SET s2 = s2 + 1;")
box.sql.execute("SELECT * FROM tj;")

box.sql.execute("DROP TABLE tj;")
