#!/usr/bin/env tarantool

test = require("sqltester")
test:plan(11)

test:do_execsql_test(
	"drop-table-fk-1.1",
	[[
		DROP TABLE IF EXISTS t3;
		DROP TABLE IF EXISTS t2;
		CREATE TABLE t2(id PRIMARY KEY);
		CREATE TABLE t3(id PRIMARY KEY REFERENCES t2(id));
		INSERT INTO t2 VALUES(1), (2), (3);
		INSERT INTO t3 VALUES(3);
	]], {
		-- <drop-table-fk-1.1>
		-- <drop-table-fk-1.1>
	})

test:do_catchsql_test(
	"drop-table-fk-1.2",
	[[
		DROP TABLE t2;
	]], {
		-- <drop-table-fk-1.2>
		1, "FOREIGN KEY constraint failed"
		-- <drop-table-fk-1.2>
	})

test:do_catchsql_test(
	"drop-table-fk-1.3",
	[[
		DROP TABLE t2;
	]], {
		-- <drop-table-fk-1.3>
		1, "FOREIGN KEY constraint failed"
		-- <drop-table-fk-1.3>
	})

test:do_execsql_test(
	"drop-table-fk-1.4",
	[[
		SELECT * FROM t2;
	]], {
		-- <drop-table-fk-1.4>
		1, 2, 3
		-- <drop-table-fk-1.4>
	})

test:do_catchsql_test(
	"drop-table-fk-1.5",
	[[
		DELETE FROM t2 WHERE id = 3;
	]], {
		-- <drop-table-fk-1.5>
		1, "FOREIGN KEY constraint failed"
		-- <drop-table-fk-1.5>
	})

test:do_execsql_test(
	"drop-table-fk-1.6",
	[[
		SELECT * FROM t2;
	]], {
		-- <drop-table-fk-1.6>
		1, 2, 3
		-- <drop-table-fk-1.6>
	})

test:do_catchsql_test(
	"drop-table-fk-1.7",
	[[
		DELETE FROM t2;
	]], {
		-- <drop-table-fk-1.7>
		1, "FOREIGN KEY constraint failed"
		-- <drop-table-fk-1.7>
	})

test:do_execsql_test(
	"drop-table-fk-1.8",
	[[
		SELECT * FROM t2;
	]], {
		-- <drop-table-fk-1.8>
		1, 2, 3
		-- <drop-table-fk-1.8>
	})

test:do_execsql_test(
	"drop-table-fk-1.9",
	[[
		DROP TABLE t3;
		DROP TABLE t2;
	]], {
		-- <drop-table-fk-1.9>
		-- <drop-table-fk-1.9>
	})

test:do_execsql_test(
	"drop-table-fk-2.1",
	[[
		CREATE TABLE t2(id PRIMARY KEY);
		CREATE TABLE t3(id PRIMARY KEY REFERENCES t2(id) ON DELETE CASCADE);
		INSERT INTO t2 VALUES(1), (2), (3);
		INSERT INTO t3 VALUES(3);
	]], {
		-- <drop-table-fk-2.1>
		-- <drop-table-fk-2.1>
	})

test:do_execsql_test(
	"drop-table-fk-2.2",
	[[
		DROP TABLE t2;
		SELECT * FROM t3;
	]], {
		-- <drop-table-fk-2.2>
		-- <drop-table-fk-2.2>
	})

test:finish_test()
