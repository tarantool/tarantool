test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

-- Test cases concerning row count calculations.
--
box.sql.execute("CREATE TABLE t1 (s1 CHAR(10) PRIMARY KEY);")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("CREATE TABLE t2 (s1 CHAR(10) PRIMARY KEY, s2 CHAR(10) REFERENCES t1 ON DELETE CASCADE);")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("CREATE TABLE t3 (i1 INT UNIQUE, i2 INT, i3 INT PRIMARY KEY);")
box.sql.execute("INSERT INTO t3 VALUES (0, 0, 0);")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("CREATE TRIGGER x AFTER DELETE ON t1 FOR EACH ROW BEGIN UPDATE t3 SET i1 = i1 + ROW_COUNT(); END;")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("INSERT INTO t1 VALUES ('a');")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("INSERT INTO t2 VALUES ('a','a');")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("INSERT INTO t1 VALUES ('b'), ('c'), ('d');")
box.sql.execute("SELECT ROW_COUNT();")
-- REPLACE is accounted for two operations: DELETE + INSERT.
box.sql.execute("REPLACE INTO t2 VALUES('a', 'c');")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("DELETE FROM t1;")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("INSERT INTO t3 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);")
box.sql.execute("TRUNCATE TABLE t3;")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("INSERT INTO t3 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);")
box.sql.execute("UPDATE t3 SET i2 = 666;")
box.sql.execute("SELECT ROW_COUNT();")
-- gh-3816: DELETE optimization returns valid number of
-- deleted tuples.
--
box.sql.execute("DELETE FROM t3 WHERE 0 = 0;")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("INSERT INTO t3 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);")
box.sql.execute("DELETE FROM t3")
box.sql.execute("SELECT ROW_COUNT();")
-- But triggers still should't be accounted.
--
box.sql.execute("CREATE TABLE tt1 (id INT PRIMARY KEY);")
box.sql.execute("CREATE TABLE tt2 (id INT PRIMARY KEY);")
box.sql.execute("CREATE TRIGGER tr1 AFTER DELETE ON tt1 BEGIN DELETE FROM tt2; END;")
box.sql.execute("INSERT INTO tt1 VALUES (1), (2), (3);")
box.sql.execute("INSERT INTO tt2 VALUES (1), (2), (3);")
box.sql.execute("DELETE FROM tt1 WHERE id = 2;")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("SELECT * FROM tt2;")
box.sql.execute("DROP TABLE tt1;")
box.sql.execute("DROP TABLE tt2;")

-- All statements which are not accounted as DML should
-- return 0 (zero) as a row count.
--
box.sql.execute("START TRANSACTION;")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("COMMIT;")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("COMMIT;")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("ANALYZE;")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute("EXPLAIN QUERY PLAN INSERT INTO t1 VALUES ('b'), ('c'), ('d');")
box.sql.execute("SELECT ROW_COUNT();")
box.sql.execute('PRAGMA recursive_triggers')

-- Clean-up.
--
box.sql.execute("DROP TABLE t2;")
box.sql.execute("DROP TABLE t3;")
box.sql.execute("DROP TABLE t1;")

