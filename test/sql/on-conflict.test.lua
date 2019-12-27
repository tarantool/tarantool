test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})
--
-- Check that original sql ON CONFLICT clause is really
-- disabled.
--
box.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT ABORT)")
box.execute("CREATE TABLE q (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT FAIL)")
box.execute("CREATE TABLE p (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT IGNORE)")
box.execute("CREATE TABLE g (id INTEGER PRIMARY KEY, v INTEGER UNIQUE ON CONFLICT REPLACE)")
box.execute("CREATE TABLE e (id INTEGER PRIMARY KEY ON CONFLICT REPLACE, v INTEGER)")
box.execute("CREATE TABLE t1(a INT PRIMARY KEY ON CONFLICT REPLACE)")
box.execute("CREATE TABLE t2(a INT PRIMARY KEY ON CONFLICT IGNORE)")

-- CHECK constraint is illegal with REPLACE option.
--
box.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, a INTEGER CHECK (a > 5) ON CONFLICT REPLACE);")

--
-- gh-3473: Primary key can't be declared with NULL.
--
box.execute("CREATE TABLE te17 (s1 INT NULL PRIMARY KEY NOT NULL);")
box.execute("CREATE TABLE te17 (s1 INT NULL PRIMARY KEY);")
box.execute("CREATE TABLE test (a int PRIMARY KEY, b int NULL ON CONFLICT IGNORE);")
box.execute("CREATE TABLE test (a int, b int NULL, c int, PRIMARY KEY(a, b, c))")

-- Several NOT NULL REPLACE constraints work
--
box.execute("CREATE TABLE a (id INT PRIMARY KEY, a INT NOT NULL ON CONFLICT REPLACE DEFAULT 1, b INT NOT NULL ON CONFLICT REPLACE DEFAULT 2);")
box.execute("INSERT INTO a VALUES(1, NULL, NULL);")
box.execute("INSERT INTO a VALUES(2, NULL, NULL);")
box.execute("SELECT * FROM a;")
box.execute("DROP TABLE a;")

-- gh-3566: UPDATE OR IGNORE causes deletion of old entry.
--
box.execute("CREATE TABLE tj (s0 INT PRIMARY KEY, s1 INT UNIQUE, s2 INT);")
box.execute("INSERT INTO tj VALUES (1, 1, 2), (2, 2, 3);")
box.execute("CREATE UNIQUE INDEX i ON tj (s2);")
box.execute("UPDATE OR IGNORE tj SET s1 = s1 + 1;")
box.execute("SELECT s1, s2 FROM tj;")
box.execute("UPDATE OR IGNORE tj SET s2 = s2 + 1;")
box.execute("SELECT s1, s2 FROM tj;")

-- gh-3565: INSERT OR REPLACE causes assertion fault.
--
box.execute("DROP TABLE tj;")
box.execute("CREATE TABLE tj (s1 INT PRIMARY KEY, s2 INT);")
box.execute("INSERT INTO tj VALUES (1, 2),(2, 3);")
box.execute("CREATE UNIQUE INDEX i ON tj (s2);")
box.execute("REPLACE INTO tj VALUES (1, 3);")
box.execute("SELECT * FROM tj;")
box.execute("INSERT INTO tj VALUES (2, 4), (3, 5);")
box.execute("UPDATE OR REPLACE tj SET s2 = s2 + 1;")
box.execute("SELECT * FROM tj;")

box.execute("DROP TABLE tj;")
