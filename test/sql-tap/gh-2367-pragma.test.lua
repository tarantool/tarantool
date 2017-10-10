#!/usr/bin/env tarantool
test = require("sqltester")

test:plan(3)

test:do_execsql_test(
	"pragma-1.1",
	[[
		PRAGMA encoding = 'utf8';
	]], {

	})

test:do_catchsql_test(
	"pragma-1.2",
	[[
		PRAGMA encoding = 'utf16le';
	]], {
		1, "unsupported encoding: utf16le"
	})

test:do_catchsql_test(
	"pragma-1.3",
	[[
		PRAGMA kek = 'ON';
	]], {
		1, "no such pragma: KEK"
	})

test:finish_test()
