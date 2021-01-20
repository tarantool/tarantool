#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(18)

--!./tcltestrunner.lua
-- 2010 September 21
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file implements tests to verify that the "testable statements" in
-- the lang_delete.html document are correct.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

test.do_delete_tests = test.do_select_tests

test:do_execsql_test(
    "e_delete-0.0",
    [[
        CREATE TABLE t1(a  INT PRIMARY KEY, b INT );
        CREATE INDEX i1 ON t1(a);
    ]], {
        -- <e_delete-0.0>

        -- </e_delete-0.0>
    })

-- -- syntax diagram delete-stmt
-- -- syntax diagram qualified-table-name
--
test:do_delete_tests("e_delete-0.1", {
    {1, "DELETE FROM t1", {}},
    {2, "DELETE FROM t1 INDEXED BY i1", {}},
    {3, "DELETE FROM t1 NOT INDEXED", {}},
    {7, "DELETE FROM t1 WHERE a>2", {}},
    {8, "DELETE FROM t1 INDEXED BY i1 WHERE a>2", {}},
    {9, "DELETE FROM t1 NOT INDEXED WHERE a>2", {}},
})
-- EVIDENCE-OF: R-20205-17349 If the WHERE clause is not present, all
-- records in the table are deleted.
--
--drop_all_tables
test:execsql "DROP TABLE t1;"
test:do_test(
    "e_delete-1.0",
    function()
        local tables = {
            "t1", "t2", "t3", "t4", "t5", "t6"
        }
        for _, t in ipairs(tables) do
            local sql = 'CREATE TABLE '..t..'(x INT PRIMARY KEY, y TEXT);'
            test:execsql(sql)
        end

        for _, t in ipairs(tables) do
            local sql = [[
                INSERT INTO TABLE_NAME VALUES(1, 'one');
                INSERT INTO TABLE_NAME VALUES(2, 'two');
                INSERT INTO TABLE_NAME VALUES(3, 'three');
                INSERT INTO TABLE_NAME VALUES(4, 'four');
                INSERT INTO TABLE_NAME VALUES(5, 'five');
            ]]
            sql = string.gsub(sql, "TABLE_NAME", t)
            test:execsql(sql)
        end
        return
    end, {
        -- <e_delete-1.0>

        -- </e_delete-1.0>
    })

test:do_delete_tests("e_delete-1.1", {
    {1, "DELETE FROM t1       ; SELECT * FROM t1", {}},
})
-- EVIDENCE-OF: R-26300-50198 If a WHERE clause is supplied, then only
-- those rows for which the WHERE clause boolean expression is true are
-- deleted.
--
-- EVIDENCE-OF: R-23360-48280 Rows for which the expression is false or
-- NULL are retained.
--
test:do_delete_tests("e_delete-1.2", {
    {1, "DELETE FROM t3 WHERE true       ; SELECT x FROM t3", {}},
    {3, "DELETE FROM t4 WHERE false    ; SELECT x FROM t4", {1, 2, 3, 4, 5}},
    {4, "DELETE FROM t4 WHERE NULL    ; SELECT x FROM t4", {1, 2, 3, 4, 5}},
    {5, "DELETE FROM t4 WHERE y!='two'; SELECT x FROM t4", {2}},
    {6, "DELETE FROM t4 WHERE y='two' ; SELECT x FROM t4", {}},
    {7, "DELETE FROM t5 WHERE x=(SELECT max(x) FROM t5);SELECT x FROM t5", {1, 2, 3, 4}},
    {8, "DELETE FROM t5 WHERE (SELECT max(x) FROM t4)  ;SELECT x FROM t5", {1, 2, 3, 4}},
    {9, "DELETE FROM t5 WHERE (SELECT max(x) FROM t6) != 0  ;SELECT x FROM t5", {}},
    {10, "DELETE FROM t6 WHERE y>'seven' ; SELECT y FROM t6", {"one", "four", "five"}},
})

test:finish_test()
