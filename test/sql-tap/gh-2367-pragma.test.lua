#!/usr/bin/env tarantool
test = require("sqltester")

test:plan(1)

test:do_catchsql_test(
	"pragma-1.3",
	[[
		PRAGMA kek = 'ON';
	]], {
		1, "no such pragma: KEK"
	})

test:finish_test()
