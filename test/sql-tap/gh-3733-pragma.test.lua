#!/usr/bin/env tarantool
test = require("sqltester")

test:plan(17)

---
--- Prerequisites
---
test:do_execsql_test(
    "pragma-0.1",
    [[
        DROP TABLE IF EXISTS gh3733;
        CREATE TABLE gh3733(id INT primary key, f float);
        INSERT INTO gh3733 VALUES(1, 0.1), (2, 0.2), (3, 0.3);
        CREATE INDEX IDX ON GH3733 (id);
    ]], {

})

---
--- pragma query_only is not supported
---
test:do_catchsql_test(
    "pragma-1.1",
    [[
        pragma query_only;
    ]], {
        1, "Pragma 'QUERY_ONLY' does not exist"
})

---
--- pragma read_uncommitted is not supported
---
test:do_catchsql_test(
	"pragma-2.1",
	[[
        pragma read_uncommitted;
    ]], {
	1, "Pragma 'READ_UNCOMMITTED' does not exist"
})

---
--- pragma index_list returns three columns in a row
---
test:do_execsql_test(
	"pragma-3.1",
	[[
        pragma index_list(gh3733)
    ]], {
	-- <pragma-3.1>
	0, 'pk_unnamed_GH3733_1', 1, 1, 'IDX', 0
	-- </pragma-3.1>
})

---
--- pragma index_list returns an empty tuple for unknown table
---
test:do_execsql_test(
	"pragma-4.1",
	[[
        pragma index_list(fufel);
    ]], {
	-- <pragma-4.1>
	-- </pragma-4.1>
})

---
--- pragma index_info returns an empty tuple for unknown index
---
test:do_execsql_test(
	"pragma-5.1",
	[[
        pragma index_info(gh3733.IDX)
    ]], {
	-- <pragma-5.1>
	0, 0, 'ID', 0, 'BINARY', 'integer'
	-- </pragma-5.1>
})

test:do_execsql_test(
	"pragma-5.2",
	[[
        pragma index_info(no_table);
    ]], {
	-- <pragma-5.2>
	-- </pragma-5.2>
})

test:do_execsql_test(
	"pragma-5.3",
	[[
        pragma index_info(wrong_table.IDX);
    ]], {
	-- <pragma-5.3>
	-- </pragma-5.3>
})

test:do_execsql_test(
	"pragma-5.4",
	[[
        pragma index_info(gh3733.wrong_index);
    ]], {
	-- <pragma-5.4>
	-- </pragma-5.4>
})

---
--- pragma sql_default_engine requires value
---
test:do_catchsql_test(
	"pragma-6.1",
	[[
        pragma sql_default_engine;
    ]], {
	1, "Illegal parameters, 'sql_default_engine' was not specified"
})

---
--- pragma sql_default_engine accepts string values and rejects IDs
---
test:do_catchsql_test(
	"pragma-7.1",
	[[
        pragma sql_default_engine(the_engine);
    ]], {
	1, "Illegal parameters, string value is expected"
})
test:do_catchsql_test(
	"pragma-7.2",
	[[
        pragma sql_default_engine(THE_ENGINE);
    ]], {
	1, "Illegal parameters, string value is expected"
})
test:do_catchsql_test(
	"pragma-7.3",
	[[
        pragma sql_default_engine("THE_ENGINE");
    ]], {
	1, "Illegal parameters, string value is expected"
})

test:do_catchsql_test(
	"pragma-7.4",
	[[
        pragma sql_default_engine('THE_ENGINE');
    ]], {
	1, "Space engine 'THE_ENGINE' does not exist"
})

test:do_catchsql_test(
	"pragma-7.5",
	[[
        pragma sql_default_engine(memtx);
    ]], {
	1, "Illegal parameters, string value is expected"
})

test:do_catchsql_test(
	"pragma-7.6",
	[[
        pragma sql_default_engine("memtx");
    ]], {
	1, "Illegal parameters, string value is expected"
})

test:do_execsql_test(
	"pragma-7.7",
	[[
        pragma sql_default_engine('memtx');
    ]], {
	-- <pragma-7.7>

	-- </pragma-7.7>
})

test:finish_test()
