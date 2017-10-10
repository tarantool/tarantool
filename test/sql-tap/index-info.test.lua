#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(4)

test:execsql([[
	CREATE TABLE t1(a INT PRIMARY KEY, b INT UNIQUE, c INT);
	INSERT INTO t1 VALUES (1, 1, 1), (2, 2, 2);
	INSERT INTO t1 VALUES (3, 3, 3), (4, 4, 4);
	CREATE INDEX t1ix1 ON t1(a);
	CREATE INDEX t1ix2 ON t1(a, b);
	CREATE INDEX t1ix3 ON t1(a, b, c);
]])

test:do_execsql_test(
	"index-info-1.1",
	"PRAGMA index_info = t1.t1ix1;",
	{
	-- <index-info-1.1>
	0, 0, 'A'
	-- <index-info-1.1>
	})

test:do_execsql_test(
	"index-info-1.2",
	"PRAGMA index_info = t1.t1ix2;",
	{
	-- <index-info-1.1>
	0, 0, 'A', 1, 1, 'B',
	-- <index-info-1.1>
	})

test:do_execsql_test(
	"index-info-1.3",
	"PRAGMA index_info = t1.t1ix3;",
	{
	-- <index-info-1.1>
	0, 0, 'A', 1, 1, 'B', 2, 2, 'C'
	-- <index-info-1.1>
	})

test:do_execsql_test(
	"index-info-1.1",
	"PRAGMA index_xinfo = t1.t1ix1;",
	{
	-- <index-info-1.1>
	0, 0, 'A', 0, 'BINARY', 1,
	-- <index-info-1.1>
	})

test:finish_test()
