#!/usr/bin/env tarantool

test = require("sqltester")
test:plan(3)

test:do_execsql_test(
	"truncate-1.1",
	[[
		CREATE TABLE te34 (s1 INT PRIMARY KEY, s2 INT);
		INSERT INTO te34 VALUES (1,1);
		CREATE TABLE te35 (s1 INT PRIMARY KEY, s2 INT);
		INSERT INTO te35 VALUES (1,1);
		CREATE TRIGGER te36 BEFORE INSERT ON te34 FOR EACH ROW BEGIN DELETE FROM te35; END;
		REPLACE INTO te34 VALUES (1,NULL);
		SELECT s1 FROM te34;
	]], {
		-- <truncate-1.1>
		1
		-- <truncate-1.1>
	})

test:do_execsql_test(
	"truncate-1.2",
	[[
		SELECT * FROM te35;
	]], {
		-- <truncate-1.2>
		-- <truncate-1.2>
	})

test:do_execsql_test(
	"truncate-1.3",
	[[
		DROP TABLE IF EXISTS t1;
		CREATE TABLE t1(id INT PRIMARY KEY, a INT, b INT);
		INSERT INTO t1 VALUES(1, 1, 1), (2, 1, 3), (3, 2, 3);
		DELETE FROM t1;
		SELECT * FROM t1;
	]], {
		-- <truncate-1.3>
		-- <truncate-1.3>
	})

test:finish_test()
