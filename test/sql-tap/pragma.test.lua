#!/usr/bin/env tarantool
local test = require("sqltester")

test:plan(10)

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
	1, "Pragma 'SQL_DEFAULT_ENGINE' does not exist"
})

--
-- gh-3733: remove useless or obsolete pragmas
--

---
--- Prerequisites
---
test:execsql(
	[[
		DROP TABLE IF EXISTS gh3733;
		CREATE TABLE gh3733(id INT primary key, f NUMBER);
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

test:finish_test()
