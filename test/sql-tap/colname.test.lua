#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(50)

--!./tcltestrunner.lua
-- 2008 July 15
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library. 
--
-- The focus of this file is testing how SQLite generates the names
-- of columns in a result set.
--
-- $Id: colname.test,v 1.7 2009/06/02 15:47:38 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Rules (applied in order):
--
-- (1) If there is an AS clause, use it.
--
-- (2) A non-trival expression (not a table column name) then the name is
--     a copy of the expression text.
--
-- (3) If short_column_names=ON, then just the abbreviated column name without
--     the table name.
--
-- (4) When short_column_names=OFF and full_column_names=OFF then
--     use case (2) for simple queries and case (5) for joins.
--
-- (5) When short_column_names=OFF and full_column_names=ON then
--     use the form: TABLE.COLUMN
--
-- Verify the default settings for short_column_name and full_column_name
--

local function lreplace(arr, pos, len, val)
    for i = pos + 1, pos + len + 1, 1 do
        arr[i] = val
    end
    return arr
end

test:do_test(
    "colname-1.1",
    function()
        return test:execsql "PRAGMA short_column_names"
    end, {
        -- <colname-1.1>
        1
        -- </colname-1.1>
    })

test:do_test(
    "colname-1.2",
    function()
        return test:execsql "PRAGMA full_column_names"
    end, {
        -- <colname-1.2>
        0
        -- </colname-1.2>
    })

-- Tests for then short=ON and full=any
--
test:do_test(
    "colname-2.1",
    function()
        test:execsql [[
            CREATE TABLE tabc(a PRIMARY KEY,b,c);
            INSERT INTO tabc VALUES(1,2,3);
            CREATE TABLE txyz(x PRIMARY KEY,y,z);
            INSERT INTO txyz VALUES(4,5,6);
            CREATE TABLE tboth(a PRIMARY KEY,b,c,x,y,z);
            INSERT INTO tboth VALUES(11,12,13,14,15,16);
            CREATE VIEW v1 AS SELECT tabC.a, txyZ.x, * 
              FROM tabc, txyz ORDER BY 1 LIMIT 1;
            CREATE VIEW v2 AS SELECT tabC.a, txyZ.x, tboTh.a, tbotH.x, *
              FROM tabc, txyz, tboth ORDER BY 1 LIMIT 1;
        ]]
        return test:execsql2 [[
            SELECT * FROM tabc;
        ]]
    end, {
        -- <colname-2.1>
        "a", 1, "b", 2, "c", 3
        -- </colname-2.1>
    })

test:do_execsql2_test(
    "colname-2.2",
    [[
        SELECT Tabc.a, tAbc.b, taBc.c, * FROM tabc
    ]], {
        -- <colname-2.2>
        "a", 1, "b", 2, "c", 3, "a", 1, "b", 2, "c", 3
        -- </colname-2.2>
    })

test:do_execsql2_test(
    "colname-2.3",
    [[
        SELECT +tabc.a, -tabc.b, tabc.c, * FROM tabc
    ]], {
        -- <colname-2.3>
        "+tabc.a", 1, "-tabc.b", -2, "c", 3, "a", 1, "b", 2, "c", 3
        -- </colname-2.3>
    })

test:do_execsql2_test(
    "colname-2.4",
    [[
        SELECT +tabc.a AS AAA, -tabc.b AS BBB, tabc.c CCC, * FROM tabc
    ]], {
        -- <colname-2.4>
        "AAA", 1, "BBB", -2, "CCC", 3, "a", 1, "b", 2, "c", 3
        -- </colname-2.4>
    })

test:do_execsql2_test(
    "colname-2.5",
    [[
        SELECT tabc.a, txyz.x, * FROM tabc, txyz;
    ]], {
        -- <colname-2.5>
        "a", 1, "x", 4, "a", 1, "b", 2, "c", 3, "x", 4, "y", 5, "z", 6
        -- </colname-2.5>
    })

test:do_execsql2_test(
    "colname-2.6",
    [[
        SELECT tabc.a, txyz.x, tabc.*, txyz.* FROM tabc, txyz;
    ]], {
        -- <colname-2.6>
        "a", 1, "x", 4, "a", 1, "b", 2, "c", 3, "x", 4, "y", 5, "z", 6
        -- </colname-2.6>
    })

test:do_execsql2_test(
    "colname-2.7",
    [[
        SELECT tabc.a, txyz.x, tboth.a, tboth.x, * FROM tabc, txyz, tboth;
    ]], {
        -- <colname-2.7>
    "a",1,"x",4,"a",11,"x",14,"a",1,"b",2,"c",3,"x",4,"y",5,"z",6,"a",11,"b",12,"c",13,"x",14,"y",15,"z",16
        -- </colname-2.7>
    })

test:do_execsql2_test(
    "colname-2.8",
    [[
        SELECT * FROM v1 ORDER BY 2;
    ]], {
        -- <colname-2.8>
        "a", 1, "x", 4, "a:1", 1, "b", 2, "c", 3, "x:1", 4, "y", 5, "z", 6
        -- </colname-2.8>
    })

test:do_execsql2_test(
    "colname-2.9",
    [[
        SELECT * FROM v2 ORDER BY 2;
    ]], {
        -- <colname-2.9>
        "a", 1, "x", 4, "a:1", 11, "x:1", 14, "a:2", 1, "b", 2, "c", 3, "x:2", 4, "y", 5, "z", 6, "a:3", 11, "b:1", 12, "c:1", 13, "x:3", 14, "y:1", 15, "z:1", 16
        -- </colname-2.9>
    })

-- Tests for short=OFF and full=OFF
test:do_test(
    "colname-3.1",
    function()
        test:execsql [[
            PRAGMA short_column_names=OFF;
            PRAGMA full_column_names=OFF;
            CREATE VIEW v3 AS SELECT tabC.a, txyZ.x, *
              FROM tabc, txyz ORDER BY 1 LIMIT 1;
            CREATE VIEW v4 AS SELECT tabC.a, txyZ.x, tboTh.a, tbotH.x, * 
              FROM tabc, txyz, tboth ORDER BY 1 LIMIT 1;
        ]]
        return test:execsql2 [[
            SELECT * FROM tabc;
        ]]
    end, {
        -- <colname-3.1>
        "a", 1, "b", 2, "c", 3
        -- </colname-3.1>
    })

test:do_execsql2_test(
    "colname-3.2",
    [[
        SELECT Tabc.a, tAbc.b, taBc.c FROM tabc
    ]], {
        -- <colname-3.2>
        "Tabc.a", 1, "tAbc.b", 2, "taBc.c", 3
        -- </colname-3.2>
    })

test:do_execsql2_test(
    "colname-3.3",
    [[
        SELECT +tabc.a, -tabc.b, tabc.c FROM tabc
    ]], {
        -- <colname-3.3>
        "+tabc.a", 1, "-tabc.b", -2, "tabc.c", 3
        -- </colname-3.3>
    })

test:do_execsql2_test(
    "colname-3.4",
    [[
        SELECT +tabc.a AS AAA, -tabc.b AS BBB, tabc.c CCC FROM tabc
    ]], {
        -- <colname-3.4>
        "AAA", 1, "BBB", -2, "CCC", 3
        -- </colname-3.4>
    })

test:do_execsql2_test(
    "colname-3.5",
    [[
        SELECT Tabc.a, Txyz.x, * FROM tabc, txyz;
    ]], {
        -- <colname-3.5>
        "Tabc.a", 1, "Txyz.x", 4, "a", 1, "b", 2, "c", 3, "x", 4, "y", 5, "z", 6
        -- </colname-3.5>
    })

test:do_execsql2_test(
    "colname-3.6",
    [[
        SELECT tabc.*, txyz.* FROM tabc, txyz;
    ]], {
        -- <colname-3.6>
        "a", 1, "b", 2, "c", 3, "x", 4, "y", 5, "z", 6
        -- </colname-3.6>
    })

test:do_execsql2_test(
    "colname-3.7",
    [[
        SELECT * FROM tabc, txyz, tboth;
    ]], {
        -- <colname-3.7>
        "a",1,"b",2,"c",3,"x",4,"y",5,"z",6,"a",11,"b",12,"c",13,"x",14,"y",15,"z",16
        -- </colname-3.7>
    })

test:do_execsql2_test(
    "colname-3.8",
    [[
        SELECT v1.a, * FROM v1 ORDER BY 2;
    ]], {
        -- <colname-3.8>
        "v1.a", 1, "a", 1, "x", 4, "a:1", 1, "b", 2, "c", 3, "x:1", 4, "y", 5, "z", 6
        -- </colname-3.8>
    })

test:do_execsql2_test(
    "colname-3.9",
    [[
        SELECT * FROM v2 ORDER BY 2;
    ]], {
        -- <colname-3.9>
        "a", 1, "x", 4, "a:1", 11, "x:1", 14, "a:2", 1, "b", 2, "c", 3, "x:2", 4, "y", 5, "z", 6, "a:3", 11, "b:1", 12, "c:1", 13, "x:3", 14, "y:1", 15, "z:1", 16
        -- </colname-3.9>
    })

test:do_execsql2_test(
    "colname-3.10",
    [[
        SELECT * FROM v3 ORDER BY 2;
    ]], {
        -- <colname-3.10>
        "a", 1, "x", 4, "a:1", 1, "b", 2, "c", 3, "x:1", 4, "y", 5, "z", 6
        -- </colname-3.10>
    })

test:do_execsql2_test(
    "colname-3.11",
    [[
        SELECT * FROM v4 ORDER BY 2;
    ]], {
        -- <colname-3.11>
        "a", 1, "x", 4, "a:1", 11, "x:1", 14, "a:2", 1, "b", 2, "c", 3, "x:2", 4, "y", 5, "z", 6, "a:3", 11, "b:1", 12, "c:1", 13, "x:3", 14, "y:1", 15, "z:1", 16
        -- </colname-3.11>
    })

-- Test for short=OFF and full=ON
test:do_test(
    "colname-4.1",
    function()
        test:execsql [[
            PRAGMA short_column_names=OFF;
            PRAGMA full_column_names=ON;
            CREATE VIEW v5 AS SELECT tabC.a, txyZ.x, *
              FROM tabc, txyz ORDER BY 1 LIMIT 1;
            CREATE VIEW v6 AS SELECT tabC.a, txyZ.x, tboTh.a, tbotH.x, * 
              FROM tabc, txyz, tboth ORDER BY 1 LIMIT 1;
        ]]
        return test:execsql2 [[
            SELECT * FROM tabc;
        ]]
    end, {
        -- <colname-4.1>
        "tabc.a", 1, "tabc.b", 2, "tabc.c", 3
        -- </colname-4.1>
    })

test:do_execsql2_test(
    "colname-4.2",
    [[
        SELECT Tabc.a, tAbc.b, taBc.c FROM tabc
    ]], {
        -- <colname-4.2>
        "tabc.a", 1, "tabc.b", 2, "tabc.c", 3
        -- </colname-4.2>
    })

test:do_execsql2_test(
    "colname-4.3",
    [[
        SELECT +tabc.a, -tabc.b, tabc.c FROM tabc
    ]], {
        -- <colname-4.3>
        "+tabc.a", 1, "-tabc.b", -2, "tabc.c", 3
        -- </colname-4.3>
    })

test:do_execsql2_test(
    "colname-4.4",
    [[
        SELECT +tabc.a AS AAA, -tabc.b AS BBB, tabc.c CCC FROM tabc
    ]], {
        -- <colname-4.4>
        "AAA", 1, "BBB", -2, "CCC", 3
        -- </colname-4.4>
    })

test:do_execsql2_test(
    "colname-4.5",
    [[
        SELECT Tabc.a, Txyz.x, * FROM tabc, txyz;
    ]], {
        -- <colname-4.5>
        "tabc.a", 1, "txyz.x", 4, "tabc.a", 1, "tabc.b", 2, "tabc.c", 3, "txyz.x", 4, "txyz.y", 5, "txyz.z", 6
        -- </colname-4.5>
    })

test:do_execsql2_test(
    "colname-4.6",
    [[
        SELECT tabc.*, txyz.* FROM tabc, txyz;
    ]], {
        -- <colname-4.6>
        "tabc.a", 1, "tabc.b", 2, "tabc.c", 3, "txyz.x", 4, "txyz.y", 5, "txyz.z", 6
        -- </colname-4.6>
    })

test:do_execsql2_test(
    "colname-4.7",
    [[
        SELECT * FROM tabc, txyz, tboth;
    ]], {
        -- <colname-4.7>
        "tabc.a", 1, "tabc.b", 2, "tabc.c", 3, "txyz.x", 4, "txyz.y", 5, "txyz.z", 6, "tboth.a", 11, "tboth.b", 12, "tboth.c", 13, "tboth.x", 14, "tboth.y", 15, "tboth.z", 16
        -- </colname-4.7>
    })

test:do_execsql2_test(
    "colname-4.8",
    [[
        SELECT * FROM v1 ORDER BY 2;
    ]], {
        -- <colname-4.8>
        "v1.a", 1, "v1.x", 4, "v1.a:1", 1, "v1.b", 2, "v1.c", 3, "v1.x:1", 4, "v1.y", 5, "v1.z", 6
        -- </colname-4.8>
    })

test:do_execsql2_test(
    "colname-4.9",
    [[
        SELECT * FROM v2 ORDER BY 2;
    ]], {
        -- <colname-4.9>
        "v2.a", 1, "v2.x", 4, "v2.a:1", 11, "v2.x:1", 14, "v2.a:2", 1, "v2.b", 2, "v2.c", 3, "v2.x:2", 4, "v2.y", 5, "v2.z", 6, "v2.a:3", 11, "v2.b:1", 12, "v2.c:1", 13, "v2.x:3", 14, "v2.y:1", 15, "v2.z:1", 16
        -- </colname-4.9>
    })

test:do_execsql2_test(
    "colname-4.10",
    [[
        SELECT * FROM v3 ORDER BY 2;
    ]], {
        -- <colname-4.10>
        "v3.a", 1, "v3.x", 4, "v3.a:1", 1, "v3.b", 2, "v3.c", 3, "v3.x:1", 4, "v3.y", 5, "v3.z", 6
        -- </colname-4.10>
    })

test:do_execsql2_test(
    "colname-4.11",
    [[
        SELECT * FROM v4 ORDER BY 2;
    ]], {
        -- <colname-4.11>
        "v4.a", 1, "v4.x", 4, "v4.a:1", 11, "v4.x:1", 14, "v4.a:2", 1, "v4.b", 2, "v4.c", 3, "v4.x:2", 4, "v4.y", 5, "v4.z", 6, "v4.a:3", 11, "v4.b:1", 12, "v4.c:1", 13, "v4.x:3", 14, "v4.y:1", 15, "v4.z:1", 16
        -- </colname-4.11>
    })

test:do_execsql2_test(
    "colname-4.12",
    [[
        SELECT * FROM v5 ORDER BY 2;
    ]], {
        -- <colname-4.12>
        "v5.a", 1, "v5.x", 4, "v5.a:1", 1, "v5.b", 2, "v5.c", 3, "v5.x:1", 4, "v5.y", 5, "v5.z", 6
        -- </colname-4.12>
    })

test:do_execsql2_test(
    "colname-4.13",
    [[
        SELECT * FROM v6 ORDER BY 2;
    ]], {
        -- <colname-4.13>
        "v6.a", 1, "v6.x", 4, "v6.a:1", 11, "v6.x:1", 14, "v6.a:2", 1, "v6.b", 2, "v6.c", 3, "v6.x:2", 4, "v6.y", 5, "v6.z", 6, "v6.a:3", 11, "v6.b:1", 12, "v6.c:1", 13, "v6.x:3", 14, "v6.y:1", 15, "v6.z:1", 16
        -- </colname-4.13>
    })

-- MUST_WORK_TEST avoid using sqlite_master
-- ticket #3229
--    test:do_test(
--        "colname-5.1",
--        function()
--            return lreplace( test:execsql("SELECT x.* FROM sqlite_master X LIMIT 1;"), 3, 3,"x")
--        end, {
--            -- <colname-5.1>
--            "table", "tabc", "tabc", "x", "CREATE TABLE tabc(a,b,c)"
--            -- </colname-5.1>
--        })

-- ticket #3370, #3371, #3372
--
test:do_test(
    "colname-6.1",
    function()
        --db("close")
        --sqlite3("db", "test.db")
        -- instead of reconnect to database
        -- we are just turning settings to default state
        test:execsql([[
            PRAGMA short_column_names=ON;
            PRAGMA full_column_names=OFF;
            ]])
        test:execsql [=[
            CREATE TABLE t6(a primary key, ['a'], ["a"], "[a]", [`a`]);
            INSERT INTO t6 VALUES(1,2,3,4,5);
        ]=]
        return test:execsql2 "SELECT * FROM t6"
    end, {
        -- <colname-6.1>
        "a", 1, "'a'", 2, [["a"]], 3, "[a]", 4, "`a`", 5
        -- </colname-6.1>
    })

test:do_execsql2_test(
    "colname-6.2",
    [=[
        SELECT ['a'], [`a`], "[a]", [a], ["a"] FROM t6
    ]=], {
        -- <colname-6.2>
        "'a'", 2, "`a`", 5, "[a]", 4, "a", 1, [["a"]], 3
        -- </colname-6.2>
    })

test:do_execsql2_test(
    "colname-6.3",
    [=[
        SELECT "'a'", "`a`", "[a]", "a", """a""" FROM t6
    ]=], {
        -- <colname-6.3>
        "'a'", 2, "`a`", 5, "[a]", 4, "a", 1, [["a"]], 3
        -- </colname-6.3>
    })

test:do_execsql2_test(
    "colname-6.4",
    [=[
        SELECT `'a'`, ```a```, `[a]`, `a`, `"a"` FROM t6
    ]=], {
        -- <colname-6.4>
        "'a'", 2, "`a`", 5, "[a]", 4, "a", 1, [["a"]], 3
        -- </colname-6.4>
    })

test:do_execsql2_test(
    "colname-6.11",
    [[
        SELECT a, max(a) AS m FROM t6
    ]], {
        -- <colname-6.11>
        "a", 1, "m", 1
        -- </colname-6.11>
    })

test:do_execsql2_test(
    "colname-6.12",
    [[
        SELECT `a`, max(a) AS m FROM t6
    ]], {
        -- <colname-6.12>
        "a", 1, "m", 1
        -- </colname-6.12>
    })

test:do_execsql2_test(
    "colname-6.13",
    [[
        SELECT "a", max(a) AS m FROM t6
    ]], {
        -- <colname-6.13>
        "a", 1, "m", 1
        -- </colname-6.13>
    })

test:do_execsql2_test(
    "colname-6.14",
    [=[
        SELECT [a], max(a) AS m FROM t6
    ]=], {
        -- <colname-6.14>
        "a", 1, "m", 1
        -- </colname-6.14>
    })

test:do_execsql2_test(
    "colname-6.15",
    [[
        SELECT t6.a, max(a) AS m FROM t6
    ]], {
        -- <colname-6.15>
        "a", 1, "m", 1
        -- </colname-6.15>
    })

test:do_execsql2_test(
    "colname-6.16",
    [=[
        SELECT ['a'], max(['a']) AS m FROM t6
    ]=], {
        -- <colname-6.16>
        "'a'", 2, "m", 2
        -- </colname-6.16>
    })

test:do_execsql2_test(
    "colname-6.17",
    [=[
        SELECT ["a"], max(["a"]) AS m FROM t6
    ]=], {
        -- <colname-6.17>
        [["a"]], 3, "m", 3
        -- </colname-6.17>
    })

test:do_execsql2_test(
    "colname-6.18",
    [=[
        SELECT "[a]", max("[a]") AS m FROM t6
    ]=], {
        -- <colname-6.18>
        "[a]", 4, "m", 4
        -- </colname-6.18>
    })

test:do_execsql2_test(
    "colname-6.19",
    [=[
        SELECT "`a`", max([`a`]) AS m FROM t6
    ]=], {
        -- <colname-6.19>
        "`a`", 5, "m", 5
        -- </colname-6.19>
    })

-- Ticket #3429
-- We cannot find anything wrong, but it never hurts to add another
-- test case.
test:do_test(
    "colname-7.1",
    function()
        test:execsql [[
            CREATE TABLE t7(x INTEGER PRIMARY KEY, y);
            INSERT INTO t7 VALUES(1,2);
        ]]
        return test:execsql2 "SELECT x, * FROM t7"
    end, {
        -- <colname-7.1>
        "x", 1, "x", 1, "y", 2
        -- </colname-7.1>
    })

-- Tickets #3893 and #3984.  (Same problem; independently reported)
--
test:do_test(
    "colname-8.1",
    function()
        return test:execsql [[
            CREATE TABLE "t3893"("x" PRIMARY KEY);
            INSERT INTO t3893 VALUES(123);
            SELECT "y"."x" FROM (SELECT "x" FROM "t3893") AS "y";
        ]]
    end, {
        -- <colname-8.1>
        123
        -- </colname-8.1>
    })



test:finish_test()
