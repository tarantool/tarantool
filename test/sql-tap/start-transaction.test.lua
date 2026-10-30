#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(21)

-----------------------------------------------------------------
-- Previously "BEGIN" / "BEGIN TRANSACTION", "COMMIT TRANSACTION"
-- / "END" / "END TRANSACTION", "ROLLBACK TRANSACTION" could be
-- used to handle transactions. By changing these commands syntax
-- in parser we're aiming on getting closer to ANSI SQL.
-----------------------------------------------------------------
-- Tarantool issue: #2164.

test:do_catchsql_test(
	"start-transaction-1.0",
	[[
		CREATE TABLE IF NOT EXISTS t(id int PRIMARY KEY);
		DELETE FROM t;
		BEGIN;
			INSERT INTO t VALUES (1);
			INSERT INTO t VALUES (2);
		COMMIT;
	]], {
		-- <start-transaction-1.0>
		1, "Syntax error at line 2 near 'BEGIN'"
		-- <start-transaction-1.0>
	})

test:do_execsql_test(
	"start-transaction-1.1",
	[[
		SELECT * FROM t;
	]], {
		-- <start-transaction-1.1>

		-- <start-transaction-1.1>
	})

test:do_catchsql_test(
	"start-transaction-1.2",
	[[
		CREATE TABLE IF NOT EXISTS t(id INT PRIMARY KEY);
		DELETE FROM t;
		BEGIN TRANSACTION;
			INSERT INTO t VALUES (1);
			INSERT INTO t VALUES (2);
		COMMIT;
	]], {
		-- <start-transaction-1.1>
		1, "Syntax error at line 2 near 'BEGIN'"
		-- <start-transaction-1.1>
	})

test:do_execsql_test(
	"start-transaction-1.3",
	[[
		SELECT * FROM t;
	]], {
		-- <start-transaction-1.3>

		-- <start-transaction-1.3>
	})

test:do_catchsql_test(
	"start-transaction-1.4",
	[[
		DELETE FROM t;
		START TRANSACTION;
			INSERT INTO t VALUES (1);
			INSERT INTO t VALUES (2);
		COMMIT;
	]], {
		-- <start-transaction-1.4>
		0
		-- <start-transaction-1.4>
	})

test:do_execsql_test(
	"start-transaction-1.5",
	[[
		SELECT * FROM t;
	]], {
		-- <start-transaction-1.5>
		1, 2
		-- <start-transaction-1.5>
	})

test:do_catchsql_test(
	"start-transaction-1.6",
	[[
		DELETE FROM t;
		START TRANSACTION;
			INSERT INTO t VALUES (1);
			INSERT INTO t VALUES (2);
		COMMIT TRANSACTION;
	]], {
		-- <start-transaction-1.6>
		1, "At line 2 at or near position 10: keyword 'TRANSACTION' is reserved. Please use double quotes if 'TRANSACTION' is an identifier."
		-- <start-transaction-1.6>
	})

test:do_execsql_test(
	"start-transaction-1.7",
	[[
		COMMIT;
		SELECT * FROM t;
	]], {
		-- <start-transaction-1.7>
		1, 2
		-- <start-transaction-1.7>
	})

test:do_catchsql_test(
	"start-transaction-1.8",
	[[
		DELETE FROM t;
		START TRANSACTION;
			INSERT INTO t VALUES (1);
			INSERT INTO t VALUES (2);
		END;
	]], {
		-- <start-transaction-1.8>
		1, "At line 2 at or near position 3: keyword 'END' is reserved. Please use double quotes if 'END' is an identifier."
		-- <start-transaction-1.8>
	})

test:do_execsql_test(
	"start-transaction-1.9",
	[[
		COMMIT;
		SELECT * FROM t;
	]], {
		-- <start-transaction-1.9>
		1, 2
		-- <start-transaction-1.9>
	})

test:do_catchsql_test(
	"start-transaction-1.10",
	[[
		DELETE FROM t;
		START TRANSACTION;
			INSERT INTO t VALUES (1);
			INSERT INTO t VALUES (2);
		END TRANSACTION;
	]], {
		-- <start-transaction-1.10>
		1, "At line 2 at or near position 3: keyword 'END' is reserved. Please use double quotes if 'END' is an identifier."
		-- <start-transaction-1.10>
	})

test:do_execsql_test(
	"start-transaction-1.11",
	[[
		COMMIT;
		SELECT * FROM t;
	]], {
		-- <start-transaction-1.11>
		1, 2
		-- <start-transaction-1.11>
	})

test:do_catchsql_test(
	"start-transaction-1.12",
	[[
		DELETE FROM t;
		START TRANSACTION;
			INSERT INTO t VALUES (1);
			INSERT INTO t VALUES (2);
		ROLLBACK;
	]], {
		-- <start-transaction-1.12>
		0
		-- <start-transaction-1.12>
	})

test:do_execsql_test(
	"start-transaction-1.13",
	[[
		SELECT * FROM t;
	]], {
		-- <start-transaction-1.13>

		-- <start-transaction-1.13>
	})

test:do_catchsql_test(
	"start-transaction-1.14",
	[[
		START TRANSACTION;
			INSERT INTO t VALUES (1);
			INSERT INTO t VALUES (2);
			ROLLBACK TRANSACTION;
		COMMIT;
	]], {
		-- <start-transaction-1.14>
		1, "At line 2 at or near position 13: keyword 'TRANSACTION' is reserved. Please use double quotes if 'TRANSACTION' is an identifier."
		-- <start-transaction-1.14>
	})

test:do_execsql_test(
	"start-transaction-1.15",
	[[
		COMMIT;
		SELECT * FROM t;
	]], {
		-- <start-transaction-1.15>
		1, 2
		-- <start-transaction-1.15>
	})

test:do_catchsql_test(
	"start-transaction-1.16",
	[[
		DELETE FROM t;
		START TRANSACTION;
			INSERT INTO t VALUES (1);
			SAVEPOINT s1;
			INSERT INTO t VALUES (2);
			ROLLBACK TO s1;
		COMMIT;
	]], {
		-- <start-transaction-1.16>
		0
		-- <start-transaction-1.16>
	})

test:do_execsql_test(
	"start-transaction-1.17",
	[[
		SELECT * FROM t;
	]], {
		-- <start-transaction-1.17>
		1
		-- <start-transaction-1.17>
	})

test:do_catchsql_test(
	"start-transaction-1.18",
	[[
		DELETE FROM t;
		START TRANSACTION;
			INSERT INTO t VALUES (1);
			SAVEPOINT s1;
			INSERT INTO t VALUES (2);
			ROLLBACK TRANSACTION TO s1;
		COMMIT;
	]], {
		-- <start-transaction-1.18>
		1, "At line 2 at or near position 13: keyword 'TRANSACTION' is reserved. Please use double quotes if 'TRANSACTION' is an identifier."
		-- <start-transaction-1.18>
	})

test:do_execsql_test(
	"start-transaction-1.19",
	[[
		COMMIT;
		SELECT * FROM t;
	]], {
		-- <start-transaction-1.19>
		1, 2
		-- <start-transaction-1.19>
	})

test:do_execsql_test(
	"start-transaction-1.20",
	[[
		DROP TABLE t;
	]], {
		-- <start0transaction-1.20>

		-- <start-transaction-1.20>
	})

test:finish_test()
