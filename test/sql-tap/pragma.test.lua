#!/usr/bin/env tarantool
test = require("sqltester")

test:plan(24)

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
	1, "Syntax error near ''memtx''"
})

test:do_catchsql_test(
	"pragma-2.5",
	[[
		pragma sql_default_engine 1;
	]], {
	1, "Syntax error near '1'"
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
	-- </pragma-3.1>
})

test:do_execsql_test(
	"pragma-3.2",
	[[
		pragma sql_default_engine='memtx';
		pragma sql_default_engine;
	]], {
	-- <pragma-3.2>
	'memtx'
	-- </pragma-3.2>
})

-- Check that "PRAGMA case_sensitive_like" returns its status
-- (0 or 1) if called without parameter.
test:do_test(
	"pragma-3.3",
	function()
		old_value = box.execute('PRAGMA case_sensitive_like').rows
		box.execute('PRAGMA case_sensitive_like = 1')
		new_value = box.execute('PRAGMA case_sensitive_like').rows
		box.execute('PRAGMA case_sensitive_like = '.. old_value[1][1])
		return new_value[1][1]
	end,
	-- <pragma-3.3>
	1
	-- </pragma-3.3>
	)

--
-- gh-3733: remove useless or obsolete pragmas
--

---
--- Prerequisites
---
test:execsql(
	[[
		DROP TABLE IF EXISTS gh3733;
		CREATE TABLE gh3733(id INT primary key, f float);
		INSERT INTO gh3733 VALUES(1, 0.1), (2, 0.2), (3, 0.3);
		CREATE INDEX IDX ON GH3733 (id);
	]])

---
--- pragma query_only is not supported
---
test:do_catchsql_test(
	"pragma-4.1",
	[[
		pragma query_only;
	]], {
	-- <pragma-4.1>
	1, "Pragma 'QUERY_ONLY' does not exist"
	-- </pragma-4.1>
})

---
--- pragma read_uncommitted is not supported
---
test:do_catchsql_test(
	"pragma-5.1",
	[[
		pragma read_uncommitted;
	]], {
	-- <pragma-5.1>
	1, "Pragma 'READ_UNCOMMITTED' does not exist"
	-- </pragma-5.1>
})

---
--- pragma index_list returns three columns in a row
---
test:do_execsql_test(
	"pragma-6.1",
	[[
		pragma index_list(gh3733)
	]], {
	-- <pragma-6.1>
	0, 'pk_unnamed_GH3733_1', 1, 1, 'IDX', 0
	-- </pragma-6.1>
})

---
--- pragma index_list returns an empty tuple for unknown table
---
test:do_execsql_test(
	"pragma-7.1",
	[[
		pragma index_list(fufel);
	]], {
	-- <pragma-7.1>
	-- </pragma-7.1>
})

---
--- pragma index_info returns an empty tuple for unknown index
---
test:do_execsql_test(
	"pragma-8.1",
	[[
		pragma index_info(gh3733.IDX)
	]], {
	-- <pragma-8.1>
	0, 0, 'ID', 0, 'BINARY', 'integer'
	-- </pragma-8.1>
})

test:do_execsql_test(
	"pragma-8.2",
	[[
		pragma index_info(no_table);
	]], {
	-- <pragma-8.2>
	-- </pragma-8.2>
})

test:do_execsql_test(
	"pragma-8.3",
	[[
		pragma index_info(wrong_table.IDX);
	]], {
	-- <pragma-8.3>
	-- </pragma-8.3>
})

test:do_execsql_test(
	"pragma-8.4",
	[[
		pragma index_info(gh3733.wrong_index);
	]], {
	-- <pragma-8.4>
	-- </pragma-8.4>
})

---
--- pragma sql_default_engine accepts string values and rejects IDs
---
test:do_catchsql_test(
	"pragma-9.1",
	[[
		pragma sql_default_engine(the_engine);
	]], {
	-- <pragma-9.1>
	1, "Illegal parameters, string value is expected"
	-- </pragma-9.1>
})

test:do_catchsql_test(
	"pragma-9.2",
	[[
		pragma sql_default_engine(THE_ENGINE);
	]], {
	-- <pragma-9.2>
	1, "Illegal parameters, string value is expected"
	-- </pragma-9.2>
})

test:do_catchsql_test(
	"pragma-9.3",
	[[
		pragma sql_default_engine("THE_ENGINE");
	]], {
	-- <pragma-9.3>
	1, "Illegal parameters, string value is expected"
	-- </pragma-9.3>
})

test:do_catchsql_test(
	"pragma-9.4",
	[[
		pragma sql_default_engine('THE_ENGINE');
	]], {
	-- <pragma-9.4>
	1, "Space engine 'THE_ENGINE' does not exist"
	-- </pragma-9.4>
})

test:do_catchsql_test(
	"pragma-9.5",
	[[
		pragma sql_default_engine(memtx);
	]], {
	-- <pragma-9.5>
	1, "Illegal parameters, string value is expected"
	-- </pragma-9.5>
})

test:do_catchsql_test(
	"pragma-9.6",
	[[
		pragma sql_default_engine("memtx");
	]], {
	-- <pragma-9.6>
	1, "Illegal parameters, string value is expected"
	-- </pragma-9.6>
})

test:do_execsql_test(
	"pragma-9.7",
	[[
		pragma sql_default_engine('memtx');
	]], {
	-- <pragma-9.7>
	-- </pragma-9.7>
})

test:finish_test()
