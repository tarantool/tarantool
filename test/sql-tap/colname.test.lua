#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(63)

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
        "A", 1, "B", 2, "C", 3
        -- </colname-2.1>
    })

test:do_execsql2_test(
    "colname-2.2",
    [[
        SELECT Tabc.a, tAbc.b, taBc.c, * FROM tabc
    ]], {
        -- <colname-2.2>
        "A", 1, "B", 2, "C", 3, "A", 1, "B", 2, "C", 3
        -- </colname-2.2>
    })

test:do_execsql2_test(
    "colname-2.3",
    [[
        SELECT +tabc.a, -tabc.b, tabc.c, * FROM tabc
    ]], {
        -- <colname-2.3>
        "+tabc.a",1,"-tabc.b",-2,"C",3,"A",1,"B",2,"C",3
        -- </colname-2.3>
    })

test:do_execsql2_test(
    "colname-2.4",
    [[
        SELECT +tabc.a AS AAA, -tabc.b AS BBB, tabc.c CCC, * FROM tabc
    ]], {
        -- <colname-2.4>
        "AAA", 1, "BBB", -2, "CCC", 3, "A", 1, "B", 2, "C", 3
        -- </colname-2.4>
    })

test:do_execsql2_test(
    "colname-2.5",
    [[
        SELECT tabc.a, txyz.x, * FROM tabc, txyz;
    ]], {
        -- <colname-2.5>
        "A", 1, "X", 4, "A", 1, "B", 2, "C", 3, "X", 4, "Y", 5, "Z", 6
        -- </colname-2.5>
    })

test:do_execsql2_test(
    "colname-2.6",
    [[
        SELECT tabc.a, txyz.x, tabc.*, txyz.* FROM tabc, txyz;
    ]], {
        -- <colname-2.6>
        "A", 1, "X", 4, "A", 1, "B", 2, "C", 3, "X", 4, "Y", 5, "Z", 6
        -- </colname-2.6>
    })

test:do_execsql2_test(
    "colname-2.7",
    [[
        SELECT tabc.a, txyz.x, tboth.a, tboth.x, * FROM tabc, txyz, tboth;
    ]], {
        -- <colname-2.7>
    "A",1,"X",4,"A",11,"X",14,"A",1,"B",2,"C",3,"X",4,"Y",5,"Z",6,"A",11,"B",12,"C",13,"X",14,"Y",15,"Z",16
        -- </colname-2.7>
    })

test:do_execsql2_test(
    "colname-2.8",
    [[
        SELECT * FROM v1 ORDER BY 2;
    ]], {
        -- <colname-2.8>
        "A", 1, "X", 4, "A:1", 1, "B", 2, "C", 3, "X:1", 4, "Y", 5, "Z", 6
        -- </colname-2.8>
    })

test:do_execsql2_test(
    "colname-2.9",
    [[
        SELECT * FROM v2 ORDER BY 2;
    ]], {
        -- <colname-2.9>
        "A", 1, "X", 4, "A:1", 11, "X:1", 14, "A:2", 1, "B", 2, "C", 3, "X:2", 4, "Y", 5, "Z", 6, "A:3", 11, "B:1", 12, "C:1", 13, "X:3", 14, "Y:1", 15, "Z:1", 16
        -- </colname-2.9>
    })

-- Tests for short=OFF and full=OFF
test:do_test(
    "colname-3.1",
    function()
        test:execsql [[
            PRAGMA short_column_names='OFF';
            PRAGMA full_column_names='OFF';
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
        "A", 1, "B", 2, "C", 3
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
        "Tabc.a", 1, "Txyz.x", 4, "A", 1, "B", 2, "C", 3, "X", 4, "Y", 5, "Z", 6
        -- </colname-3.5>
    })

test:do_execsql2_test(
    "colname-3.6",
    [[
        SELECT tabc.*, txyz.* FROM tabc, txyz;
    ]], {
        -- <colname-3.6>
        "A", 1, "B", 2, "C", 3, "X", 4, "Y", 5, "Z", 6
        -- </colname-3.6>
    })

test:do_execsql2_test(
    "colname-3.7",
    [[
        SELECT * FROM tabc, txyz, tboth;
    ]], {
        -- <colname-3.7>
        "A",1,"B",2,"C",3,"X",4,"Y",5,"Z",6,"A",11,"B",12,"C",13,"X",14,"Y",15,"Z",16
        -- </colname-3.7>
    })

test:do_execsql2_test(
    "colname-3.8",
    [[
        SELECT v1.a, * FROM v1 ORDER BY 2;
    ]], {
        -- <colname-3.8>
        "v1.a", 1, "A", 1, "X", 4, "A:1", 1, "B", 2, "C", 3, "X:1", 4, "Y", 5, "Z", 6
        -- </colname-3.8>
    })

test:do_execsql2_test(
    "colname-3.9",
    [[
        SELECT * FROM v2 ORDER BY 2;
    ]], {
        -- <colname-3.9>
        "A", 1, "X", 4, "A:1", 11, "X:1", 14, "A:2", 1, "B", 2, "C", 3, "X:2", 4, "Y", 5, "Z", 6, "A:3", 11, "B:1", 12, "C:1", 13, "X:3", 14, "Y:1", 15, "Z:1", 16
        -- </colname-3.9>
    })

test:do_execsql2_test(
    "colname-3.10",
    [[
        SELECT * FROM v3 ORDER BY 2;
    ]], {
        -- <colname-3.10>
        "A", 1, "X", 4, "A:1", 1, "B", 2, "C", 3, "X:1", 4, "Y", 5, "Z", 6
        -- </colname-3.10>
    })

test:do_execsql2_test(
    "colname-3.11",
    [[
        SELECT * FROM v4 ORDER BY 2;
    ]], {
        -- <colname-3.11>
        "A", 1, "X", 4, "A:1", 11, "X:1", 14, "A:2", 1, "B", 2, "C", 3, "X:2", 4, "Y", 5, "Z", 6, "A:3", 11, "B:1", 12, "C:1", 13, "X:3", 14, "Y:1", 15, "Z:1", 16
        -- </colname-3.11>
    })

-- Test for short=OFF and full=ON
test:do_test(
    "colname-4.1",
    function()
        test:execsql [[
            PRAGMA short_column_names='OFF';
            PRAGMA full_column_names='ON';
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
        "TABC.A", 1, "TABC.B", 2, "TABC.C", 3
        -- </colname-4.1>
    })

test:do_execsql2_test(
    "colname-4.2",
    [[
        SELECT Tabc.a, tAbc.b, taBc.c FROM tabc
    ]], {
        -- <colname-4.2>
        "TABC.A", 1, "TABC.B", 2, "TABC.C", 3
        -- </colname-4.2>
    })

test:do_execsql2_test(
    "colname-4.3",
    [[
        SELECT +tabc.a, -tabc.b, tabc.c FROM tabc
    ]], {
        -- <colname-4.3>
        "+tabc.a", 1, "-tabc.b", -2, "TABC.C", 3
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
        "TABC.A", 1, "TXYZ.X", 4, "TABC.A", 1, "TABC.B", 2, "TABC.C", 3, "TXYZ.X", 4, "TXYZ.Y", 5, "TXYZ.Z", 6
        -- </colname-4.5>
    })

test:do_execsql2_test(
    "colname-4.6",
    [[
        SELECT tabc.*, txyz.* FROM tabc, txyz;
    ]], {
        -- <colname-4.6>
        "TABC.A", 1, "TABC.B", 2, "TABC.C", 3, "TXYZ.X", 4, "TXYZ.Y", 5, "TXYZ.Z", 6
        -- </colname-4.6>
    })

test:do_execsql2_test(
    "colname-4.7",
    [[
        SELECT * FROM tabc, txyz, tboth;
    ]], {
        -- <colname-4.7>
        "TABC.A", 1, "TABC.B", 2, "TABC.C", 3, "TXYZ.X", 4, "TXYZ.Y", 5, "TXYZ.Z", 6, "TBOTH.A", 11, "TBOTH.B", 12, "TBOTH.C", 13, "TBOTH.X", 14, "TBOTH.Y", 15, "TBOTH.Z", 16
        -- </colname-4.7>
    })

test:do_execsql2_test(
    "colname-4.8",
    [[
        SELECT * FROM v1 ORDER BY 2;
    ]], {
        -- <colname-4.8>
        "V1.A", 1, "V1.X", 4, "V1.A:1", 1, "V1.B", 2, "V1.C", 3, "V1.X:1", 4, "V1.Y", 5, "V1.Z", 6
        -- </colname-4.8>
    })

test:do_execsql2_test(
    "colname-4.9",
    [[
        SELECT * FROM v2 ORDER BY 2;
    ]], {
        -- <colname-4.9>
        "V2.A", 1, "V2.X", 4, "V2.A:1", 11, "V2.X:1", 14, "V2.A:2", 1, "V2.B", 2, "V2.C", 3, "V2.X:2", 4, "V2.Y", 5, "V2.Z", 6, "V2.A:3", 11, "V2.B:1", 12, "V2.C:1", 13, "V2.X:3", 14, "V2.Y:1", 15, "V2.Z:1", 16
        -- </colname-4.9>
    })

test:do_execsql2_test(
    "colname-4.10",
    [[
        SELECT * FROM v3 ORDER BY 2;
    ]], {
        -- <colname-4.10>
        "V3.A", 1, "V3.X", 4, "V3.A:1", 1, "V3.B", 2, "V3.C", 3, "V3.X:1", 4, "V3.Y", 5, "V3.Z", 6
        -- </colname-4.10>
    })

test:do_execsql2_test(
    "colname-4.11",
    [[
        SELECT * FROM v4 ORDER BY 2;
    ]], {
        -- <colname-4.11>
        "V4.A", 1, "V4.X", 4, "V4.A:1", 11, "V4.X:1", 14, "V4.A:2", 1, "V4.B", 2, "V4.C", 3, "V4.X:2", 4, "V4.Y", 5, "V4.Z", 6, "V4.A:3", 11, "V4.B:1", 12, "V4.C:1", 13, "V4.X:3", 14, "V4.Y:1", 15, "V4.Z:1", 16
        -- </colname-4.11>
    })

test:do_execsql2_test(
    "colname-4.12",
    [[
        SELECT * FROM v5 ORDER BY 2;
    ]], {
        -- <colname-4.12>
        "V5.A", 1, "V5.X", 4, "V5.A:1", 1, "V5.B", 2, "V5.C", 3, "V5.X:1", 4, "V5.Y", 5, "V5.Z", 6
        -- </colname-4.12>
    })

test:do_execsql2_test(
    "colname-4.13",
    [[
        SELECT * FROM v6 ORDER BY 2;
    ]], {
        -- <colname-4.13>
        "V6.A", 1, "V6.X", 4, "V6.A:1", 11, "V6.X:1", 14, "V6.A:2", 1, "V6.B", 2, "V6.C", 3, "V6.X:2", 4, "V6.Y", 5, "V6.Z", 6, "V6.A:3", 11, "V6.B:1", 12, "V6.C:1", 13, "V6.X:3", 14, "V6.Y:1", 15, "V6.Z:1", 16
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
            PRAGMA short_column_names='ON';
            PRAGMA full_column_names='OFF';
            ]])
        test:execsql [=[
            CREATE TABLE t6(a primary key, "'a'", """a""", "[a]", "`a`");
            INSERT INTO t6 VALUES(1,2,3,4,5);
        ]=]
        return test:execsql2 "SELECT * FROM t6"
    end, {
        -- <colname-6.1>
        "A", 1, "'a'", 2, [["a"]], 3, "[a]", 4, "`a`", 5
        -- </colname-6.1>
    })

test:do_execsql2_test(
    "colname-6.3",
    [=[
        SELECT "'a'", "`a`", "[a]", "A", """a""" FROM t6
    ]=], {
        -- <colname-6.3>
        "'a'", 2, "`a`", 5, "[a]", 4, "A", 1, [["a"]], 3
        -- </colname-6.3>
    })

test:do_execsql2_test(
    "colname-6.11",
    [[
        SELECT a, max(a) AS m FROM t6
    ]], {
        -- <colname-6.11>
        "A", 1, "M", 1
        -- </colname-6.11>
    })

test:do_execsql2_test(
    "colname-6.13",
    [[
        SELECT a, max(a) AS m FROM t6
    ]], {
        -- <colname-6.13>
        "A", 1, "M", 1
        -- </colname-6.13>
    })

test:do_execsql2_test(
    "colname-6.15",
    [[
        SELECT t6.a, max(a) AS m FROM t6
    ]], {
        -- <colname-6.15>
        "A", 1, "M", 1
        -- </colname-6.15>
    })

test:do_execsql2_test(
    "colname-6.18",
    [=[
        SELECT "[a]", max("[a]") AS m FROM t6
    ]=], {
        -- <colname-6.18>
        "[a]", 4, "M", 4
        -- </colname-6.18>
    })

test:do_execsql2_test(
    "colname-6.19",
    [=[
        SELECT "`a`", max("`a`") AS m FROM t6
    ]=], {
        -- <colname-6.19>
        "`a`", 5, "M", 5
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
        "X", 1, "X", 1, "Y", 2
        -- </colname-7.1>
    })

-- Tickets #3893 and #3984.  (Same problem; independently reported)
--
test:do_test(
    "colname-8.1",
    function()
        return test:execsql [[
            CREATE TABLE t3893("x" PRIMARY KEY);
            INSERT INTO t3893 VALUES(123);
            SELECT "y"."x" FROM (SELECT "x" FROM t3893) AS "y";
        ]]
    end, {
        -- <colname-8.1>
        123
        -- </colname-8.1>
    })

local data = {
    [[`a`]],
    "[a]",
}
for i, val in ipairs(data) do
    test:do_catchsql_test(
        "colname-9.1."..i,
        string.format("SELECT %s FROM t6", val),
        {1, "/unrecognized token/"}
    )
end

local data2 = {
    {[['a']],{1, "/syntax error/"}}, -- because ' is delimiter for strings
    {[[`a`]],{1, "/unrecognized token/"}}, -- because ` is undefined symbol
    {"[a]",{1, "/unrecognized token/"}} -- because [ is undefined symbol
}
for i, val in ipairs(data2) do
    test:do_catchsql_test(
        "colname-9.2"..i,
        string.format("create table %s(a primary key)", val[1]),
        val[2]
    )
end


for i, val in ipairs(data2) do
    test:do_catchsql_test(
        "colname-9.3."..i,
        string.format("create table a(%s primary key)", val[1]),
        val[2]
    )
end

test:execsql([[ create table table1("a" primary key, "b") ]])
test:execsql("insert into table1 values('a1', 'a1')")

-- " is used for identifiers
-- ' is used for strings

local data3 = {
    --{1, [["a1" = "b1"]], {0, {}}}, -- should be error: column does not exist?
    {2, [["a" = 'a1']], {0, {"a1", "a1"}}},
    {3, [['a' = 'a1']], {0, {}}},
    {4, [["a" = "b"]], {0, {"a1", "a1"}}},
    {5, [['a1' = "a"]], {0, {"a1", "a1"}}},
    {6, [['a' = "a"]], {0, {}}},
    {7, [[ "a" = "a"]], {0, {"a1", "a1"}}},
}

for _, val in ipairs(data3) do
    test:do_catchsql_test(
        "colname-10.1."..val[1],
        string.format([[select * from table1 where %s]], val[2]),
        val[3])
end

test:do_test(
    "colname-11.0.1",
    function ()
        test:drop_all_tables()
    end,
    nil)

test:do_test(
    "colname-11.0.2",
    function ()
        test:drop_all_views()
    end,
    nil)

test:do_catchsql_test(
    "colname-11.1",
    [[ create table t1(a, b, c, primary key('A'))]],
    {1, "expressions prohibited in PRIMARY KEY"})

test:do_catchsql_test(
    "colname-11.2",
    [[CREATE TABLE t1(a, b, c, d, e, 
      PRIMARY KEY(a), UNIQUE('b' COLLATE "unicode_ci" DESC));]],
    {1, "/functional indexes aren't supported in the current version/"})

test:execsql("create table table1(a primary key, b, c)")

test:do_catchsql_test(
    "colname-11.3",
    [[ CREATE INDEX t1c ON table1('c'); ]],
    {1, "/functional indexes aren't supported in the current version/"})

test:finish_test()
