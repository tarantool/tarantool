#!/usr/bin/env tarantool
test = require("sqltester")

test:plan(9)

test:do_catchsql_test(
	"pragma-1.3",
	[[
		PRAGMA kek = 'ON';
	]], {
		1, "Pragma 'KEK' does not exist"
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

--
-- gh-3832: Some statements do not return column type
--
-- Check that "PRAGMA sql_default_engine" called without arguments
-- returns currently set sql_default_engine.
test:do_execsql_test(
	"pragma-3.1",
	[[
		pragma sql_default_engine='vinyl';
		pragma sql_default_engine;
	]], {
	-- <pragma-3.1>
	'vinyl'
	-- <pragma-3.1>
})

test:do_execsql_test(
	"pragma-3.2",
	[[
		pragma sql_default_engine='memtx';
		pragma sql_default_engine;
	]], {
	-- <pragma-3.2>
	'memtx'
	-- <pragma-3.2>
})

-- Check that "PRAGMA case_sensitive_like" returns its status
-- (0 or 1) if called without parameter.
test:do_test(
	"pragma-3.3",
	function()
		old_value = box.sql.execute('PRAGMA case_sensitive_like')
		box.sql.execute('PRAGMA case_sensitive_like = 1')
		new_value = box.sql.execute('PRAGMA case_sensitive_like')
		box.sql.execute('PRAGMA case_sensitive_like = '.. old_value[1][1])
		return new_value[1][1]
	end,
	-- <pragma-3.3>
	1
	-- <pragma-3.3>
	)

test:finish_test()
