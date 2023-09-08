#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(5)

test:execsql " DROP TABLE IF EXISTS t1 "
test:execsql " DROP TABLE IF EXISTS t2 "

test:execsql " CREATE TABLE t1(a INT PRIMARY KEY, b INT, c INT) "
test:execsql " CREATE TABLE t2(a INT PRIMARY KEY, b INT, c INT) "

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
    1, "Index 'same_index_name' already exists in space 't1'"
	-- <index-1.3>
	})

test:do_catchsql_test(
	"index-1.4",
	[[
		CREATE INDEX same_index_name ON t2(b);
	]],
	{
	-- <index-1.4>
    1, "Index 'same_index_name' already exists in space 't2'"
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

test:finish_test()
