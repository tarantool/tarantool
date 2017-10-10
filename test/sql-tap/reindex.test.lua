#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(3)


test:execsql("CREATE TABLE t1(a INT PRIMARY KEY);")
test:execsql("CREATE INDEX t1ix1 ON t1(a)")

test:do_execsql_test(
	"reindex-1.1",
	"REINDEX t1ix1 ON t1",
	{
	-- <reindex-1.1>
	
	-- <reindex-1.1>
	})

test:do_catchsql_test(
	"reindex-1.2",
	"REINDEX t1ix2 ON t1",
	{
		-- <reindex-1.1>
		1, "unable to identify the object to be reindexed"
		-- <reindex-1.1>
	})

test:do_catchsql_test(
	"reindex-1.3",
	"REINDEX t1ix1 ON t3",
	{
		-- <reindex-1.1>
		1, "no such table: T3"
		-- <reindex-1.1>
	})

test:finish_test()
