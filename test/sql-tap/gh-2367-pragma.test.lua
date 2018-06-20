#!/usr/bin/env tarantool
test = require("sqltester")

test:plan(7)

test:do_catchsql_test(
	"pragma-1.3",
	[[
		PRAGMA kek = 'ON';
	]], {
		1, "no such pragma: KEK"
	})

---
--- gh-2199: SQL default engine pragma
---
test:do_catchsql_test(
	"pragma-2.1",
	[[
		pragma sql_default_engine='creepy';
	]], {
	1, "Space engine 'creepy' does not exist"
})

test:do_catchsql_test(
	"pragma-2.2",
	[[
		pragma sql_default_engine='vinyl';
	]], {
	0
})

test:do_catchsql_test(
	"pragma-2.3",
	[[
		pragma sql_default_engine='memtx';
	]], {
	0
})

test:do_catchsql_test(
	"pragma-2.4",
	[[
		pragma sql_default_engine;
	]], {
	1, 'Illegal parameters, \'sql_default_engine\' was not specified'
})

test:do_catchsql_test(
	"pragma-2.5",
	[[
		pragma sql_default_engine 'memtx';
	]], {
	1, 'near \"\'memtx\'\": syntax error'
})

test:do_catchsql_test(
	"pragma-2.5",
	[[
		pragma sql_default_engine 1;
	]], {
	1, 'near \"1\": syntax error'
})

test:finish_test()
