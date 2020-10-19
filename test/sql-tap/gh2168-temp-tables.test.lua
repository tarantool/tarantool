#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(3)

test:do_catchsql_test(
	"trigger2-10.1",
	[[
		CREATE TEMP TABLE tmp1 (id INTEGER PRIMARY KEY);
	]], {
		-- <trigger2-10.1>
	1, "Syntax error at line 1 near 'TEMP'"
		-- <trigger2-10.1>
});

-- TEMP triggers are removed now, check it

test:do_execsql_test(
	"trigger2-10.2",
	[[
		CREATE TABLE t1 (id INTEGER PRIMARY KEY);
	]], {
		-- <trigger2-10.2>
		-- <trigger2-10.2>
});

test:do_catchsql_test(
	"trigger2-10.3",
	[[
		CREATE TEMP TRIGGER ttmp1 BEFORE UPDATE ON t1
		BEGIN
			SELECT 1;
		END;
	]], {
		-- <trigger2-10.3>
	1, "Syntax error at line 1 near 'TEMP'"
		-- <trigger2-10.3>
});


test:finish_test()
