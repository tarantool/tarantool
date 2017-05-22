#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(16)

--!./tcltestrunner.lua
-- 2001 September 15
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.  The
-- focus of this file is testing the SELECT statement.
--
-- $Id: select2.test,v 1.28 2009/01/15 15:23:59 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Create a table with some data
--
test:execsql "DROP TABLE IF EXISTS tbl1"
test:execsql "CREATE TABLE tbl1(id int, f1 int, f2 int, primary key(id))"
test:execsql "BEGIN"
for i = 0, 30, 1 do
    test:execsql(string.format("INSERT INTO tbl1 VALUES(%s, %s,%s)", i, i%9, i%10))
end
test:execsql "COMMIT"
-- Do a second query inside a first.
--
test:do_test(
    "select2-1.1",
    function()
        local r = {}
        local data = test:execsql "SELECT DISTINCT f1 FROM tbl1 ORDER BY f1"
        for _, f1 in ipairs(data) do
            table.insert(r, f1..":")
            local data = test:execsql( string.format("SELECT f2 FROM tbl1 WHERE f1=%s ORDER BY f2", f1))
            for _, f2 in ipairs(data) do
                table.insert(r, f2)
            end
        end
        return r
    end, {
        -- <select2-1.1>
        "0:", 0, 7, 8, 9, "1:", 0, 1, 8, 9, "2:", 0, 1, 2, 9, "3:", 0, 1, 2, 3, "4:", 2, 3, 4, "5:", 3, 4, 5, "6:", 4, 5, 6, "7:", 5, 6, 7, "8:", 6, 7, 8
        -- </select2-1.1>
    })

test:do_test(
    "select2-1.2",
    function()
        local r = {}
        local data = test:execsql "SELECT DISTINCT f1 FROM tbl1 WHERE f1>3 AND f1<5"
        for _, f1 in ipairs(data) do
            table.insert(r, f1..":")
            local data = test:execsql( string.format("SELECT f2 FROM tbl1 WHERE f1=%s ORDER BY f2", f1))
            for _, f2 in ipairs(data) do
                table.insert(r, f2)
            end
        end
        return r
    end, {
        -- <select2-1.2>
        "4:", 2, 3, 4
        -- </select2-1.2>
    })

-- it was test for tcl cache
-- only time comparition was removed from tcl version
test:execsql "DROP TABLE if exists tbl2"
test:do_test(
    "select2-2.0.2",
    function()
        test:execsql "CREATE TABLE tbl2(f1 int primary key, f2 int, f3 int); BEGIN;"
        for i = 1, 30000, 1 do
            test:execsql( string.format("INSERT INTO tbl2 VALUES(%s,%s,%s)",i, i*2, i*3))
        end
        test:execsql("COMMIT");
        return {}
    end, {
        -- <select2-2.0.2>
        -- </select2-2.0.2>
    })

test:do_execsql_test(
    "select2-2.1",
    [[
        SELECT count(*) FROM tbl2
    ]], {
        -- <select2-2.1>
        30000
        -- </select2-2.1>
    })

test:do_execsql_test(
    "select2-2.2",
    [[
        SELECT count(*) FROM tbl2 WHERE f2>1000
    ]], {
        -- <select2-2.2>
        29500
        -- </select2-2.2>
    })

test:do_execsql_test(
    "select2-3.1",
    [[
        SELECT f1 FROM tbl2 WHERE 1000=f2
    ]], {
        -- <select2-3.1>
        500
        -- </select2-3.1>
    })

test:do_execsql_test(
    "select2-3.2a",
    [[
        CREATE INDEX idx1 ON tbl2(f2)
    ]], {
        -- <select2-3.2a>
        
        -- </select2-3.2a>
    })

test:do_execsql_test(
    "select2-3.2b",
    [[
        SELECT f1 FROM tbl2 WHERE 1000=f2
    ]], {
        -- <select2-3.2b>
        500
        -- </select2-3.2b>
    })

test:do_execsql_test(
    "select2-3.2c",
    [[
        SELECT f1 FROM tbl2 WHERE f2=1000
    ]], {
        -- <select2-3.2c>
        500
        -- </select2-3.2c>
    })

-- TODO: need access to sqlite_search_count vatiable
--test:do_test(
--    "select2-3.2d",
--    function()
--        sqlite_search_count = 0
--        test:execsql "SELECT * FROM tbl2 WHERE 1000=f2"
--        return sqlite_search_count
--    end, {
--        -- <select2-3.2d>
--        1
--        -- </select2-3.2d>
--    })

-- TODO: need access to sqlite_search_count vatiable
--test:do_test(
--    "select2-3.2e",
--    function()
--        sqlite_search_count = 0
--        test:execsql "SELECT * FROM tbl2 WHERE f2=1000"
--        return sqlite_search_count
--    end, {
--        -- <select2-3.2e>
--        1
--        -- </select2-3.2e>
--    })

-- TODO: need access to sqlite_search_count vatiable
-- Make sure queries run faster with an index than without
--
--test:do_test(
--    "select2-3.3",
--    function()
--        test:execsql "DROP INDEX idx1"
--        sqlite_search_count = 0
--        test:execsql "SELECT f1 FROM tbl2 WHERE f2==2000"
--        return sqlite_search_count
--    end, {
--        -- <select2-3.3>
--        29999
--        -- </select2-3.3>
--    })

-- Make sure we can optimize functions in the WHERE clause that
-- use fields from two or more different table.  (Bug #6)
--
test:do_execsql_test(
    "select2-4.1",
    [[
        DROP TABLE IF EXISTS aa;
        DROP TABLE IF EXISTS bb;
        CREATE TABLE aa(a int primary key);
        CREATE TABLE bb(b int primary key);
        INSERT INTO aa VALUES(1);
        INSERT INTO aa VALUES(3);
        INSERT INTO bb VALUES(2);
        INSERT INTO bb VALUES(4);
        SELECT * FROM aa, bb WHERE max(a,b)>2;
    ]], {
        -- <select2-4.1>
        1, 4, 3, 2, 3, 4
        -- </select2-4.1>
    })

test:do_execsql_test(
    "select2-4.2",
    [[
        INSERT INTO bb VALUES(0);
        SELECT * FROM aa CROSS JOIN bb WHERE b;
    ]], {
        -- <select2-4.2>
        1, 2, 1, 4, 3, 2, 3, 4
        -- </select2-4.2>
    })

test:do_execsql_test(
    "select2-4.3",
    [[
        SELECT * FROM aa CROSS JOIN bb WHERE NOT b;
    ]], {
        -- <select2-4.3>
        1, 0, 3, 0
        -- </select2-4.3>
    })

test:do_execsql_test(
    "select2-4.4",
    [[
        SELECT * FROM aa, bb WHERE min(a,b);
    ]], {
        -- <select2-4.4>
        1, 2, 1, 4, 3, 2, 3, 4
        -- </select2-4.4>
    })

test:do_execsql_test(
    "select2-4.5",
    [[
        SELECT * FROM aa, bb WHERE NOT min(a,b);
    ]], {
        -- <select2-4.5>
        1, 0, 3, 0
        -- </select2-4.5>
    })

test:do_execsql_test(
    "select2-4.6",
    [[
        SELECT * FROM aa, bb WHERE CASE WHEN a=b-1 THEN 1 END;
    ]], {
        -- <select2-4.6>
        1, 2, 3, 4
        -- </select2-4.6>
    })

test:do_execsql_test(
    "select2-4.7",
    [[
        SELECT * FROM aa, bb WHERE CASE WHEN a=b-1 THEN 0 ELSE 1 END;
    ]], {
        -- <select2-4.7>
        1, 0, 1, 4, 3, 0, 3, 2
        -- </select2-4.7>
    })

test:finish_test()

