#!/usr/bin/env tarantool

-- this test will be deleted in scope of #3195
test = require("sqltester")
test:plan(3)

test:execsql("DROP TABLE IF EXISTS t1");
test:execsql("CREATE TABLE t1(a INT PRIMARY KEY)");
test:execsql("CREATE INDEX i1 on t1(a)");

test:do_catchsql_test(
	"1",
	"REINDEX i1 ON t1",
	{1, "keyword \"REINDEX\" is reserved"}
)

test:do_catchsql_test(
	"2",
	"REINDEX t1",
	 {1, "keyword \"REINDEX\" is reserved"}
)

test:do_catchsql_test(
	"3",
	"REINDEX",
	{1, "keyword \"REINDEX\" is reserved"}
)

test:finish_test()
