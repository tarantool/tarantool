test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')

-- Test cases concerning row count calculations.
--
box.execute("CREATE TABLE t1 (s1 VARCHAR(10) PRIMARY KEY);")
box.execute("SELECT ROW_COUNT();")
box.execute("SELECT ROW_COUNT();")
box.execute("CREATE TABLE t2 (s1 VARCHAR(10) PRIMARY KEY, s2 VARCHAR(10) REFERENCES t1 ON DELETE CASCADE);")
box.execute("SELECT ROW_COUNT();")
box.execute("CREATE TABLE t3 (i1 INT UNIQUE, i2 INT, i3 INT PRIMARY KEY);")
box.execute("INSERT INTO t3 VALUES (0, 0, 0);")
box.execute("SELECT ROW_COUNT();")
box.execute("CREATE TRIGGER x AFTER DELETE ON t1 FOR EACH ROW BEGIN UPDATE t3 SET i1 = i1 + ROW_COUNT(); END;")
box.execute("SELECT ROW_COUNT();")
box.execute("INSERT INTO t1 VALUES ('a');")
box.execute("SELECT ROW_COUNT();")
box.execute("INSERT INTO t2 VALUES ('a','a');")
box.execute("SELECT ROW_COUNT();")
box.execute("INSERT INTO t1 VALUES ('b'), ('c'), ('d');")
box.execute("SELECT ROW_COUNT();")
-- REPLACE is accounted for two operations: DELETE + INSERT.
box.execute("REPLACE INTO t2 VALUES('a', 'c');")
box.execute("SELECT ROW_COUNT();")
box.execute("DELETE FROM t1;")
box.execute("SELECT ROW_COUNT();")
box.execute("INSERT INTO t3 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);")
box.execute("TRUNCATE TABLE t3;")
box.execute("SELECT ROW_COUNT();")
box.execute("INSERT INTO t3 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);")
box.execute("UPDATE t3 SET i2 = 666;")
box.execute("SELECT ROW_COUNT();")
-- gh-3816: DELETE optimization returns valid number of
-- deleted tuples.
--
box.execute("DELETE FROM t3 WHERE 0 = 0;")
box.execute("SELECT ROW_COUNT();")
box.execute("INSERT INTO t3 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);")
box.execute("DELETE FROM t3")
box.execute("SELECT ROW_COUNT();")
-- But triggers still should't be accounted.
--
box.execute("CREATE TABLE tt1 (id INT PRIMARY KEY);")
box.execute("CREATE TABLE tt2 (id INT PRIMARY KEY);")
box.execute("CREATE TRIGGER tr1 AFTER DELETE ON tt1 FOR EACH ROW BEGIN DELETE FROM tt2; END;")
box.execute("INSERT INTO tt1 VALUES (1), (2), (3);")
box.execute("INSERT INTO tt2 VALUES (1), (2), (3);")
box.execute("DELETE FROM tt1 WHERE id = 2;")
box.execute("SELECT ROW_COUNT();")
box.execute("SELECT * FROM tt2;")
box.execute("DROP TABLE tt1;")
box.execute("DROP TABLE tt2;")

-- All statements which are not accounted as DML should
-- return 0 (zero) as a row count.
--
box.execute("START TRANSACTION;")
box.execute("SELECT ROW_COUNT();")
box.execute("COMMIT;")
box.execute("SELECT ROW_COUNT();")
box.execute("COMMIT;")
box.execute("SELECT ROW_COUNT();")
-- box.execute("ANALYZE;")
box.execute("SELECT ROW_COUNT();")
box.execute("EXPLAIN QUERY PLAN INSERT INTO t1 VALUES ('b'), ('c'), ('d');")
box.execute("SELECT ROW_COUNT();")
box.execute('PRAGMA recursive_triggers')

-- Clean-up.
--
box.execute("DROP TABLE t2;")
box.execute("DROP TABLE t3;")
box.execute("DROP TABLE t1;")

