#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(2)

test:do_catchsql_test(
	"trigger2-10.1",
	[[
		CREATE TEMP TABLE tmp1 (id INTEGER PRIMARY KEY);
	]], {
		-- <trigger2-10.1>
	1, "Syntax error near 'TEMP'"
		-- <trigger2-10.1>
});

-- TEMP triggers are removed now, check it
test:do_catchsql_test(
	"trigger2-10.1",
	[[
		CREATE TABLE t1 (id INTEGER PRIMARY KEY);
		CREATE TEMP TRIGGER ttmp1 BEFORE UPDATE ON t1
		BEGIN
			SELECT 1;
		END;
	]], {
		-- <trigger2-10.1>
	1, "Syntax error near 'TEMP'"
		-- <trigger2-10.1>
});


test:finish_test()
