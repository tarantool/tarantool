#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(76)

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
-- This file implements regression tests for sql library.
--
-- The focus of this file is testing how sql generates the names
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
-- (3) If full_column_names=OFF, then just the abbreviated column name without
--     the table name.
--
-- (4) When full_column_names=ON then use the form: TABLE.COLUMN
--
-- Verify the default settings for full_column_name
--
local function lreplace(arr, pos, len, val) -- luacheck: no unused
    for i = pos + 1, pos + len + 1, 1 do
        arr[i] = val
    end
    return arr
end

test:do_test(
    "colname-1.1",
    function()
        return box.space._session_settings:get("sql_full_column_names").value
    end,
        -- <colname-1.2>
        false
        -- </colname-1.2>
    )

-- Tests for then short=ON and full=any
--
test:do_test(
    "colname-2.1",
    function()
        test:execsql [[
            CREATE TABLE tabc(a INT PRIMARY KEY,b INT,c INT);
            INSERT INTO tabc VALUES(1,2,3);
            CREATE TABLE txyz(x INT PRIMARY KEY,y INT,z INT);
            INSERT INTO txyz VALUES(4,5,6);
            CREATE TABLE tboth(a INT PRIMARY KEY,b INT,c INT,x INT,y INT,z INT);
            INSERT INTO tboth VALUES(11,12,13,14,15,16);
            CREATE VIEW v1 AS SELECT tabc.a, txyz.x, *
              FROM tabc, txyz ORDER BY 1 LIMIT 1;
            CREATE VIEW v2 AS SELECT tabc.a, txyz.x, tboth.a, tboth.x, *
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
        SELECT tabc.a, tabc.b, tabc.c, * from tabc
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
        "COLUMN_1", 1, "COLUMN_2", -2, "c", 3, "a", 1, "b", 2, "c", 3
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
    "a", 1, "x", 4, "a", 11, "x", 14, "a", 1, "b", 2, "c", 3, "x", 4, "y", 5,
    "z", 6, "a", 11, "b", 12, "c", 13, "x", 14, "y", 15, "z", 16
        -- </colname-2.7>
    })

test:do_execsql2_test(
    "colname-2.8",
    [[
        SELECT * FROM v1 ORDER BY 2;
    ]], {
        -- <colname-2.8>
        "a",1,"x",4,"a_1",1,"b",2,"c",3,"x_1",4,"y",5,"z",6
        -- </colname-2.8>
    })

test:do_execsql2_test(
    "colname-2.9",
    [[
        SELECT * FROM v2 ORDER BY 2;
    ]], {
        -- <colname-2.9>
        "a", 1, "x", 4, "a_1", 11, "x_1", 14, "a_2", 1, "b", 2, "c", 3,
        "x_2", 4, "y", 5, "z", 6, "a_3", 11, "b_1", 12, "c_1", 13, "x_3", 14,
        "y_1", 15, "z_1", 16
        -- </colname-2.9>
    })

-- Tests for full=OFF
test:do_test(
    "colname-3.1",
    function()
        test:execsql [[
            UPDATE "_session_settings" SET "value" = false WHERE "name" = 'sql_full_column_names';
            CREATE VIEW v3 AS SELECT tabc.a, txyz.x, *
              FROM tabc, txyz ORDER BY 1 LIMIT 1;
            CREATE VIEW v4 AS SELECT tabc.a, txyz.x, tboth.a, tboth.x, *
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
        SELECT tabc.a, tabc.b, tabc.c from tabc
    ]], {
        -- <colname-3.2>
        "a", 1, "b", 2, "c", 3
        -- </colname-3.2>
    })

test:do_execsql2_test(
    "colname-3.3",
    [[
        SELECT +tabc.a, -tabc.b, tabc.c FROM tabc
    ]], {
        -- <colname-3.3>
        "COLUMN_1", 1, "COLUMN_2", -2, "c", 3
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
        SELECT tabc.a, txyz.x, * from tabc, txyz;
    ]], {
        -- <colname-3.5>
        "a", 1, "x", 4, "a", 1, "b", 2, "c", 3, "x", 4, "y", 5, "z", 6
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
        "a", 1, "b", 2, "c", 3, "x", 4, "y", 5, "z", 6, "a", 11, "b", 12,
        "c", 13, "x", 14, "y", 15, "z", 16
        -- </colname-3.7>
    })

test:do_execsql2_test(
    "colname-3.8",
    [[
        SELECT v1.a, * FROM v1 ORDER BY 2;
    ]], {
        -- <colname-3.8>
        "a", 1, "a", 1, "x", 4, "a_1", 1, "b", 2, "c", 3, "x_1", 4, "y", 5,
        "z", 6
        -- </colname-3.8>
    })

test:do_execsql2_test(
    "colname-3.9",
    [[
        SELECT * FROM v2 ORDER BY 2;
    ]], {
        -- <colname-3.9>
        "a", 1, "x", 4, "a_1", 11, "x_1", 14, "a_2", 1, "b", 2, "c", 3,
        "x_2", 4, "y", 5, "z", 6, "a_3", 11, "b_1", 12, "c_1", 13, "x_3", 14,
        "y_1", 15, "z_1", 16
        -- </colname-3.9>
    })

test:do_execsql2_test(
    "colname-3.10",
    [[
        SELECT * FROM v3 ORDER BY 2;
    ]], {
        -- <colname-3.10>
        "a", 1, "x", 4, "a_1", 1, "b", 2, "c", 3, "x_1", 4, "y", 5, "z", 6
        -- </colname-3.10>
    })

test:do_execsql2_test(
    "colname-3.11",
    [[
        SELECT * FROM v4 ORDER BY 2;
    ]], {
        -- <colname-3.11>
        "a", 1, "x", 4, "a_1", 11, "x_1", 14, "a_2", 1, "b", 2, "c", 3,
        "x_2", 4, "y", 5, "z", 6, "a_3", 11, "b_1", 12, "c_1", 13, "x_3", 14,
        "y_1", 15, "z_1", 16
        -- </colname-3.11>
    })

-- Test for full=ON
test:do_test(
    "colname-4.1",
    function()
        test:execsql [[
            UPDATE "_session_settings" SET "value" = true WHERE "name" = 'sql_full_column_names';
            CREATE VIEW v5 AS SELECT tabc.a, txyz.x, *
              FROM tabc, txyz ORDER BY 1 LIMIT 1;
            CREATE VIEW v6 AS SELECT tabc.a, txyz.x, tboth.a, tboth.x, *
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
        SELECT tabc.a, tabc.b, tabc.c from tabc
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
        "COLUMN_1", 1, "COLUMN_2", -2, "tabc.c", 3
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
        SELECT tabc.a, txyz.x, * from tabc, txyz;
    ]], {
        -- <colname-4.5>
        "tabc.a", 1, "txyz.x", 4, "tabc.a", 1, "tabc.b", 2, "tabc.c", 3,
        "txyz.x", 4, "txyz.y", 5, "txyz.z", 6
        -- </colname-4.5>
    })

test:do_execsql2_test(
    "colname-4.6",
    [[
        SELECT tabc.*, txyz.* FROM tabc, txyz;
    ]], {
        -- <colname-4.6>
        "tabc.a", 1, "tabc.b", 2, "tabc.c", 3, "txyz.x", 4, "txyz.y", 5,
        "txyz.z", 6
        -- </colname-4.6>
    })

test:do_execsql2_test(
    "colname-4.7",
    [[
        SELECT * FROM tabc, txyz, tboth;
    ]], {
        -- <colname-4.7>
        "tabc.a", 1, "tabc.b", 2, "tabc.c", 3, "txyz.x", 4, "txyz.y", 5,
        "txyz.z", 6, "tboth.a", 11, "tboth.b", 12, "tboth.c", 13,
        "tboth.x", 14, "tboth.y", 15, "tboth.z", 16
        -- </colname-4.7>
    })

test:do_execsql2_test(
    "colname-4.8",
    [[
        SELECT * FROM v1 ORDER BY 2;
    ]], {
        -- <colname-4.8>
        "v1.a", 1, "v1.x", 4, "v1.a_1", 1, "v1.b", 2, "v1.c", 3, "v1.x_1", 4,
        "v1.y", 5, "v1.z", 6
        -- </colname-4.8>
    })

test:do_execsql2_test(
    "colname-4.9",
    [[
        SELECT * FROM v2 ORDER BY 2;
    ]], {
        -- <colname-4.9>
        "v2.a", 1, "v2.x", 4, "v2.a_1", 11, "v2.x_1", 14, "v2.a_2", 1,
        "v2.b", 2, "v2.c", 3, "v2.x_2", 4, "v2.y", 5, "v2.z", 6, "v2.a_3", 11,
        "v2.b_1", 12, "v2.c_1", 13, "v2.x_3", 14, "v2.y_1", 15, "v2.z_1", 16
        -- </colname-4.9>
    })

test:do_execsql2_test(
    "colname-4.10",
    [[
        SELECT * FROM v3 ORDER BY 2;
    ]], {
        -- <colname-4.10>
        "v3.a", 1, "v3.x", 4, "v3.a_1", 1, "v3.b", 2, "v3.c", 3, "v3.x_1", 4,
        "v3.y", 5, "v3.z", 6
        -- </colname-4.10>
    })

test:do_execsql2_test(
    "colname-4.11",
    [[
        SELECT * FROM v4 ORDER BY 2;
    ]], {
        -- <colname-4.11>
        "v4.a", 1, "v4.x", 4, "v4.a_1", 11, "v4.x_1", 14, "v4.a_2", 1,
        "v4.b", 2, "v4.c", 3, "v4.x_2", 4, "v4.y", 5, "v4.z", 6, "v4.a_3", 11,
        "v4.b_1", 12, "v4.c_1", 13, "v4.x_3", 14, "v4.y_1", 15, "v4.z_1", 16
        -- </colname-4.11>
    })

test:do_execsql2_test(
    "colname-4.12",
    [[
        SELECT * FROM v5 ORDER BY 2;
    ]], {
        -- <colname-4.12>
        "v5.a", 1, "v5.x", 4, "v5.a_1", 1, "v5.b", 2, "v5.c", 3, "v5.x_1", 4,
        "v5.y", 5, "v5.z", 6
        -- </colname-4.12>
    })

test:do_execsql2_test(
    "colname-4.13",
    [[
        SELECT * FROM v6 ORDER BY 2;
    ]], {
        -- <colname-4.13>
        "v6.a", 1, "v6.x", 4, "v6.a_1", 11, "v6.x_1", 14, "v6.a_2", 1,
        "v6.b", 2, "v6.c", 3, "v6.x_2", 4, "v6.y", 5, "v6.z", 6, "v6.a_3", 11,
        "v6.b_1", 12, "v6.c_1", 13, "v6.x_3", 14, "v6.y_1", 15, "v6.z_1", 16
        -- </colname-4.13>
    })

-- MUST_WORK_TEST avoid using sql_master
-- ticket #3229
--    test:do_test(
--        "colname-5.1",
--        function()
--            return lreplace( test:execsql("SELECT x.* FROM sql_master X LIMIT 1;"), 3, 3,"x")
--        end, {
--            -- <colname-5.1>
--            "table", "tabc", "tabc", "x", "CREATE TABLE tabc(a INT,b INT,c INT)"
--            -- </colname-5.1>
--        })

-- ticket #3370, #3371, #3372
--
test:do_test(
    "colname-6.1",
    function()
        -- instead of reconnect to database
        -- we are just turning settings to default state
        test:execsql([[
            UPDATE "_session_settings" SET "value" = false WHERE "name" = 'sql_full_column_names';
            ]])
        test:execsql [=[
            CREATE TABLE t6(a INT primary key, "'a'" INT, """a""" INT, "[a]" INT,  "`a`" INT);
            INSERT INTO t6 VALUES(1,2,3,4,5);
        ]=]
        return test:execsql2 "SELECT * FROM t6"
    end, {
        -- <colname-6.1>
        "a", 1, "'a'", 2, [["a"]], 3, "[a]", 4, "`a`", 5
        -- </colname-6.1>
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
    "colname-6.11",
    [[
        SELECT a, MAX(a) AS m FROM t6
    ]], {
        -- <colname-6.11>
        "a", 1, "m", 1
        -- </colname-6.11>
    })

test:do_execsql2_test(
    "colname-6.13",
    [[
        SELECT a, MAX(a) AS m FROM t6
    ]], {
        -- <colname-6.13>
        "a", 1, "m", 1
        -- </colname-6.13>
    })

test:do_execsql2_test(
    "colname-6.15",
    [[
        SELECT t6.a, MAX(a) AS m FROM t6
    ]], {
        -- <colname-6.15>
        "a", 1, "m", 1
        -- </colname-6.15>
    })

test:do_execsql2_test(
    "colname-6.18",
    [=[
        SELECT "[a]", MAX("[a]") AS m FROM t6
    ]=], {
        -- <colname-6.18>
        "[a]", 4, "m", 4
        -- </colname-6.18>
    })

test:do_execsql2_test(
    "colname-6.19",
    [=[
        SELECT "`a`", MAX("`a`") AS m FROM t6
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
            CREATE TABLE t7(x INTEGER PRIMARY KEY, y INT);
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
            CREATE TABLE t3893("x" INT PRIMARY KEY);
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
}
for i, val in ipairs(data) do
    test:do_catchsql_test(
        "colname-9.1."..i,
        string.format("SELECT %s FROM t6", val),
        {1, "/unrecognized token/"}
    )
end

local data2 = {
    {[['a']],{1, "/Syntax error/"}}, -- because ' is delimiter for strings
    {[[`a`]],{1, "/unrecognized token/"}}, -- because ` is undefined symbol
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

test:execsql([[ create table table1("a" TEXT primary key, "b" TEXT) ]])
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
        test:drop_all_views()
    end,
    nil)

test:do_test(
    "colname-11.0.2",
    function ()
        test:drop_all_tables()
    end,
    nil)

test:do_catchsql_test(
    "colname-11.1",
    [[ create table t1(a INT, b INT, c INT, primary key('A'))]],
    {1, "Expressions are prohibited in an index definition"})

test:do_catchsql_test(
    "colname-11.2",
    [[CREATE TABLE t1(a INT, b INT, c INT, d INT, e INT,
      PRIMARY KEY(a), UNIQUE('b' COLLATE "unicode_ci" DESC));]],
    {1, "Expressions are prohibited in an index definition"})

test:execsql("create table table1(a  INT primary key, b INT, c INT)")

test:do_catchsql_test(
    "colname-11.3",
    [[ CREATE INDEX t1c ON table1('c'); ]],
    {1, "Expressions are prohibited in an index definition"})

--
-- gh-3962: Check auto generated names in different selects.
--
test:do_execsql2_test(
    "colname-12.1",
    [[
        VALUES(1, 2, 'aaa');
    ]], {
        "COLUMN_1",1,"COLUMN_2",2,"COLUMN_3","aaa"
    })

test:do_execsql2_test(
    "colname-12.2",
    [[
        SELECT * FROM (VALUES (1+1, 1+1, 'aaa'));
    ]], {
        "COLUMN_1",2,"COLUMN_2",2,"COLUMN_3","aaa"
    })

test:do_execsql2_test(
    "colname-12.3",
    [[
        SELECT 1+1, 1+1, 'aaa';
    ]], {
        "COLUMN_1",2,"COLUMN_2",2,"COLUMN_3","aaa"
    })

test:do_execsql2_test(
    "colname-12.4",
    [[
        SELECT * FROM (SELECT * FROM (VALUES(1, 2, 'aaa'))),
                      (SELECT * FROM (VALUES(1, 2, 'aaa')))
    ]], {
        "COLUMN_1",1,"COLUMN_2",2,"COLUMN_3","aaa","COLUMN_4",1,"COLUMN_5",2,
        "COLUMN_6","aaa"
    })

test:do_execsql2_test(
    "colname-12.5",
    [[
        CREATE TABLE j (s1 INTEGER PRIMARY KEY);
        INSERT INTO j VALUES(1);
    ]], {})

--
-- Column named as 'COLUMN_1', because 's1 + 1' is a expression.
--
test:do_execsql2_test(
    "colname-12.6",
    [[
        SELECT s1 + 1 FROM j;
    ]], {
        "COLUMN_1",2
    })

test:do_execsql2_test(
    "colname-12.7",
    [[
        SELECT s1 + 1 FROM j ORDER BY COLUMN_1;
    ]], {
        "COLUMN_1",2
    })

test:do_execsql2_test(
    "colname-12.8",
    [[
        SELECT * FROM (SELECT s1 + 1 FROM j
                       ORDER BY COLUMN_1) ORDER BY COLUMN_1;
    ]], {
        "COLUMN_1",2
    })

test:do_execsql2_test(
    "colname-12.9",
    [[
        SELECT s1 + 1 FROM j GROUP BY COLUMN_1;
    ]], {
        "COLUMN_1",2
    })

test:do_execsql2_test(
    "colname-12.10",
    [[
        SELECT * FROM (SELECT s1 + 1 FROM j
                       ORDER BY COLUMN_1) GROUP BY COLUMN_1;
    ]], {
        "COLUMN_1",2
    })

test:do_execsql2_test(
    "colname-12.11",
    [[
        SELECT * FROM (SELECT s1 + 1 FROM j
                       ORDER BY COLUMN_1) WHERE COLUMN_1 = 2;
    ]], {
        "COLUMN_1",2
    })

test:do_execsql2_test(
    "colname-12.12",
    [[
        SELECT *, s1 + 1 FROM j ORDER BY COLUMN_1;
    ]], {
        "s1",1,"COLUMN_1",2
    })

test:do_execsql2_test(
    "colname-12.13",
    [[
        SELECT s1 + 1, * FROM j ORDER BY COLUMN_1;
    ]], {
        "COLUMN_1",2,"s1",1
    })

test:do_execsql2_test(
    "colname-12.14",
    [[
        CREATE TABLE j_1 (COLUMN_1 INTEGER PRIMARY KEY, COLUMN_2 SCALAR);
        INSERT INTO j_1 VALUES(1, 1);
    ]], {})

test:do_execsql2_test(
    "colname-12.15",
    [[
        SELECT COLUMN_1, COLUMN_1 + 1, COLUMN_2, 2 FROM j_1;
    ]], {
        "COLUMN_1",1,"COLUMN_1",2,"COLUMN_2",1,"COLUMN_2",2
    })

--
-- The result order is different, because in the second case
-- expression "-COLUMN_1" with the auto generated name "COLUMN_1"
-- is used for sorting. Auto generated names are considered as
-- aliases. In the process of resolving an identifier from
-- <ORDER BY>, it first checks for matching with aliases.
--
test:do_execsql2_test(
    "colname-12.16",
    [[
        INSERT INTO j_1 VALUES(2, 2);
        SELECT COLUMN_1 FROM j_1 ORDER BY COLUMN_1;
    ]], {
        "COLUMN_1",1,"COLUMN_1",2
    })

test:do_execsql2_test(
    "colname-12.17",
    [[
        SELECT COLUMN_1, -COLUMN_1 FROM j_1 ORDER BY COLUMN_1;
    ]], {
        "COLUMN_1",2,"COLUMN_1",-2,"COLUMN_1",1,"COLUMN_1",-1
    })

test:finish_test()
