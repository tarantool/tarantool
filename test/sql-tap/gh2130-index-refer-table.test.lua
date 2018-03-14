#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(5)

test:execsql " DROP TABLE IF EXISTS t1 "
test:execsql " DROP TABLE IF EXISTS t2 "

test:execsql " CREATE TABLE t1(a INT PRIMARY KEY, b, c) "
test:execsql " CREATE TABLE t2(a INT PRIMARY KEY, b, c) "

test:do_execsql_test(
	"index-1.1",
	[[
		CREATE INDEX t1ix1 ON t1(a);
		CREATE INDEX t1ix2 ON t1(b);
		CREATE INDEX t2ix1 ON t2(a);
		CREATE INDEX t2ix2 ON t2(b);
	]],
	{
	-- <index-1.1>

	-- <index-1.1>
	})
	
test:do_execsql_test(
	"index-1.2",
	[[
		CREATE INDEX same_index_name ON t1(a, b);
		CREATE INDEX same_index_name ON t2(a, b);
	]],
	{
	-- <index-1.2>

	-- <index-1.2>
	})

test:do_catchsql_test(
	"index-1.3",
	[[
		CREATE INDEX same_index_name ON t1(b);
	]],
	{
	-- <index-1.3>
	1, "index T1.SAME_INDEX_NAME already exists"
	-- <index-1.3>
	})

test:do_catchsql_test(
	"index-1.4",
	[[
		CREATE INDEX same_index_name ON t2(b);
	]],
	{
	-- <index-1.4>
	1, "index T2.SAME_INDEX_NAME already exists"
	-- <index-1.4>
	})


test:do_execsql_test(
	"index-1.5",
	[[
		DROP INDEX t1ix1 ON t1;
		CREATE INDEX t1ix1 ON t1(a, b);
	]],
	{
	-- <index-1.5>

	-- <index-1.5>
	})

-- This part of test is banned in scope of #2174
-- test:do_execsql_test(
--	"index-1.6",
--	[[
--		REINDEX t1ix1 ON t1;
--	]],
--	{
--	-- <index-1.6>
--
--	-- <index-1.6>
--	})

test:finish_test()
