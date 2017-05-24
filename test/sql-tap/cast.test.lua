#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(69)

--!./tcltestrunner.lua
-- 2005 June 25
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
-- focus of this file is testing the CAST operator.
--
-- $Id: cast.test,v 1.10 2008/11/06 15:33:04 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Only run these tests if the build includes the CAST operator


-- Tests for the CAST( AS blob), CAST( AS text) and CAST( AS numeric) built-ins
--
test:do_execsql_test(
    "cast-1.1",
    [[
        SELECT x'616263'
    ]], {
        -- <cast-1.1>
        "abc"
        -- </cast-1.1>
    })

test:do_execsql_test(
    "cast-1.2",
    [[
        SELECT typeof(x'616263')
    ]], {
        -- <cast-1.2>
        "blob"
        -- </cast-1.2>
    })

test:do_execsql_test(
    "cast-1.3",
    [[
        SELECT CAST(x'616263' AS text)
    ]], {
        -- <cast-1.3>
        "abc"
        -- </cast-1.3>
    })

test:do_execsql_test(
    "cast-1.4",
    [[
        SELECT typeof(CAST(x'616263' AS text))
    ]], {
        -- <cast-1.4>
        "text"
        -- </cast-1.4>
    })

test:do_execsql_test(
    "cast-1.5",
    [[
        SELECT CAST(x'616263' AS numeric)
    ]], {
        -- <cast-1.5>
        0
        -- </cast-1.5>
    })

test:do_execsql_test(
    "cast-1.6",
    [[
        SELECT typeof(CAST(x'616263' AS numeric))
    ]], {
        -- <cast-1.6>
        "integer"
        -- </cast-1.6>
    })

test:do_execsql_test(
    "cast-1.7",
    [[
        SELECT CAST(x'616263' AS blob)
    ]], {
        -- <cast-1.7>
        "abc"
        -- </cast-1.7>
    })

test:do_execsql_test(
    "cast-1.8",
    [[
        SELECT typeof(CAST(x'616263' AS blob))
    ]], {
        -- <cast-1.8>
        "blob"
        -- </cast-1.8>
    })

test:do_execsql_test(
    "cast-1.9",
    [[
        SELECT CAST(x'616263' AS integer)
    ]], {
        -- <cast-1.9>
        0
        -- </cast-1.9>
    })

test:do_execsql_test(
    "cast-1.10",
    [[
        SELECT typeof(CAST(x'616263' AS integer))
    ]], {
        -- <cast-1.10>
        "integer"
        -- </cast-1.10>
    })



test:do_execsql_test(
    "cast-1.11",
    [[
        SELECT null
    ]], {
        -- <cast-1.11>
        ""
        -- </cast-1.11>
    })

test:do_execsql_test(
    "cast-1.12",
    [[
        SELECT typeof(NULL)
    ]], {
        -- <cast-1.12>
        "null"
        -- </cast-1.12>
    })

test:do_execsql_test(
    "cast-1.13",
    [[
        SELECT CAST(NULL AS text)
    ]], {
        -- <cast-1.13>
        ""
        -- </cast-1.13>
    })

test:do_execsql_test(
    "cast-1.14",
    [[
        SELECT typeof(CAST(NULL AS text))
    ]], {
        -- <cast-1.14>
        "null"
        -- </cast-1.14>
    })

test:do_execsql_test(
    "cast-1.15",
    [[
        SELECT CAST(NULL AS numeric)
    ]], {
        -- <cast-1.15>
        ""
        -- </cast-1.15>
    })

test:do_execsql_test(
    "cast-1.16",
    [[
        SELECT typeof(CAST(NULL AS numeric))
    ]], {
        -- <cast-1.16>
        "null"
        -- </cast-1.16>
    })

test:do_execsql_test(
    "cast-1.17",
    [[
        SELECT CAST(NULL AS blob)
    ]], {
        -- <cast-1.17>
        ""
        -- </cast-1.17>
    })

test:do_execsql_test(
    "cast-1.18",
    [[
        SELECT typeof(CAST(NULL AS blob))
    ]], {
        -- <cast-1.18>
        "null"
        -- </cast-1.18>
    })

test:do_execsql_test(
    "cast-1.19",
    [[
        SELECT CAST(NULL AS integer)
    ]], {
        -- <cast-1.19>
        ""
        -- </cast-1.19>
    })

test:do_execsql_test(
    "cast-1.20",
    [[
        SELECT typeof(CAST(NULL AS integer))
    ]], {
        -- <cast-1.20>
        "null"
        -- </cast-1.20>
    })

test:do_execsql_test(
    "cast-1.21",
    [[
        SELECT 123
    ]], {
        -- <cast-1.21>
        123
        -- </cast-1.21>
    })

test:do_execsql_test(
    "cast-1.22",
    [[
        SELECT typeof(123)
    ]], {
        -- <cast-1.22>
        "integer"
        -- </cast-1.22>
    })

test:do_execsql_test(
    "cast-1.23",
    [[
        SELECT CAST(123 AS text)
    ]], {
        -- <cast-1.23>
        "123"
        -- </cast-1.23>
    })

test:do_execsql_test(
    "cast-1.24",
    [[
        SELECT typeof(CAST(123 AS text))
    ]], {
        -- <cast-1.24>
        "text"
        -- </cast-1.24>
    })

test:do_execsql_test(
    "cast-1.25",
    [[
        SELECT CAST(123 AS numeric)
    ]], {
        -- <cast-1.25>
        123
        -- </cast-1.25>
    })

test:do_execsql_test(
    "cast-1.26",
    [[
        SELECT typeof(CAST(123 AS numeric))
    ]], {
        -- <cast-1.26>
        "integer"
        -- </cast-1.26>
    })

test:do_execsql_test(
    "cast-1.27",
    [[
        SELECT CAST(123 AS blob)
    ]], {
        -- <cast-1.27>
        "123"
        -- </cast-1.27>
    })

test:do_execsql_test(
    "cast-1.28",
    [[
        SELECT typeof(CAST(123 AS blob))
    ]], {
        -- <cast-1.28>
        "blob"
        -- </cast-1.28>
    })

test:do_execsql_test(
    "cast-1.29",
    [[
        SELECT CAST(123 AS integer)
    ]], {
        -- <cast-1.29>
        123
        -- </cast-1.29>
    })

test:do_execsql_test(
    "cast-1.30",
    [[
        SELECT typeof(CAST(123 AS integer))
    ]], {
        -- <cast-1.30>
        "integer"
        -- </cast-1.30>
    })

test:do_execsql_test(
    "cast-1.31",
    [[
        SELECT 123.456
    ]], {
        -- <cast-1.31>
        123.456
        -- </cast-1.31>
    })

test:do_execsql_test(
    "cast-1.32",
    [[
        SELECT typeof(123.456)
    ]], {
        -- <cast-1.32>
        "real"
        -- </cast-1.32>
    })

test:do_execsql_test(
    "cast-1.33",
    [[
        SELECT CAST(123.456 AS text)
    ]], {
        -- <cast-1.33>
        "123.456"
        -- </cast-1.33>
    })

test:do_execsql_test(
    "cast-1.34",
    [[
        SELECT typeof(CAST(123.456 AS text))
    ]], {
        -- <cast-1.34>
        "text"
        -- </cast-1.34>
    })

test:do_execsql_test(
    "cast-1.35",
    [[
        SELECT CAST(123.456 AS numeric)
    ]], {
        -- <cast-1.35>
        123.456
        -- </cast-1.35>
    })

test:do_execsql_test(
    "cast-1.36",
    [[
        SELECT typeof(CAST(123.456 AS numeric))
    ]], {
        -- <cast-1.36>
        "real"
        -- </cast-1.36>
    })

test:do_execsql_test(
    "cast-1.37",
    [[
        SELECT CAST(123.456 AS blob)
    ]], {
        -- <cast-1.37>
        "123.456"
        -- </cast-1.37>
    })

test:do_execsql_test(
    "cast-1.38",
    [[
        SELECT typeof(CAST(123.456 AS blob))
    ]], {
        -- <cast-1.38>
        "blob"
        -- </cast-1.38>
    })

test:do_execsql_test(
    "cast-1.39",
    [[
        SELECT CAST(123.456 AS integer)
    ]], {
        -- <cast-1.39>
        123
        -- </cast-1.39>
    })

test:do_execsql_test(
    "cast-1.38",
    [[
        SELECT typeof(CAST(123.456 AS integer))
    ]], {
        -- <cast-1.38>
        "integer"
        -- </cast-1.38>
    })

test:do_execsql_test(
    "cast-1.41",
    [[
        SELECT '123abc'
    ]], {
        -- <cast-1.41>
        "123abc"
        -- </cast-1.41>
    })

test:do_execsql_test(
    "cast-1.42",
    [[
        SELECT typeof('123abc')
    ]], {
        -- <cast-1.42>
        "text"
        -- </cast-1.42>
    })

test:do_execsql_test(
    "cast-1.43",
    [[
        SELECT CAST('123abc' AS text)
    ]], {
        -- <cast-1.43>
        "123abc"
        -- </cast-1.43>
    })

test:do_execsql_test(
    "cast-1.44",
    [[
        SELECT typeof(CAST('123abc' AS text))
    ]], {
        -- <cast-1.44>
        "text"
        -- </cast-1.44>
    })

test:do_execsql_test(
    "cast-1.45",
    [[
        SELECT CAST('123abc' AS numeric)
    ]], {
        -- <cast-1.45>
        123
        -- </cast-1.45>
    })

test:do_execsql_test(
    "cast-1.46",
    [[
        SELECT typeof(CAST('123abc' AS numeric))
    ]], {
        -- <cast-1.46>
        "integer"
        -- </cast-1.46>
    })

test:do_execsql_test(
    "cast-1.47",
    [[
        SELECT CAST('123abc' AS blob)
    ]], {
        -- <cast-1.47>
        "123abc"
        -- </cast-1.47>
    })

test:do_execsql_test(
    "cast-1.48",
    [[
        SELECT typeof(CAST('123abc' AS blob))
    ]], {
        -- <cast-1.48>
        "blob"
        -- </cast-1.48>
    })

test:do_execsql_test(
    "cast-1.49",
    [[
        SELECT CAST('123abc' AS integer)
    ]], {
        -- <cast-1.49>
        123
        -- </cast-1.49>
    })

test:do_execsql_test(
    "cast-1.50",
    [[
        SELECT typeof(CAST('123abc' AS integer))
    ]], {
        -- <cast-1.50>
        "integer"
        -- </cast-1.50>
    })

test:do_execsql_test(
    "cast-1.51",
    [[
        SELECT CAST('123.5abc' AS numeric)
    ]], {
        -- <cast-1.51>
        123.5
        -- </cast-1.51>
    })

test:do_execsql_test(
    "cast-1.53",
    [[
        SELECT CAST('123.5abc' AS integer)
    ]], {
        -- <cast-1.53>
        123
        -- </cast-1.53>
    })

test:do_execsql_test(
    "case-1.60",
    [[
        SELECT CAST(null AS REAL)
    ]], {
        -- <case-1.60>
        ""
        -- </case-1.60>
    })

test:do_execsql_test(
    "case-1.61",
    [[
        SELECT typeof(CAST(null AS REAL))
    ]], {
        -- <case-1.61>
        "null"
        -- </case-1.61>
    })

test:do_execsql_test(
    "case-1.62",
    [[
        SELECT CAST(1 AS REAL)
    ]], {
        -- <case-1.62>
        1.0
        -- </case-1.62>
    })

test:do_execsql_test(
    "case-1.63",
    [[
        SELECT typeof(CAST(1 AS REAL))
    ]], {
        -- <case-1.63>
        "real"
        -- </case-1.63>
    })

test:do_execsql_test(
    "case-1.64",
    [[
        SELECT CAST('1' AS REAL)
    ]], {
        -- <case-1.64>
        1.0
        -- </case-1.64>
    })

test:do_execsql_test(
    "case-1.65",
    [[
        SELECT typeof(CAST('1' AS REAL))
    ]], {
        -- <case-1.65>
        "real"
        -- </case-1.65>
    })

test:do_execsql_test(
    "case-1.66",
    [[
        SELECT CAST('abc' AS REAL)
    ]], {
        -- <case-1.66>
        0.0
        -- </case-1.66>
    })

test:do_execsql_test(
    "case-1.67",
    [[
        SELECT typeof(CAST('abc' AS REAL))
    ]], {
        -- <case-1.67>
        "real"
        -- </case-1.67>
    })

test:do_execsql_test(
    "case-1.68",
    [[
        SELECT CAST(x'31' AS REAL)
    ]], {
        -- <case-1.68>
        1.0
        -- </case-1.68>
    })

test:do_execsql_test(
    "case-1.69",
    [[
        SELECT typeof(CAST(x'31' AS REAL))
    ]], {
        -- <case-1.69>
        "real"
        -- </case-1.69>
    })

-- Ticket #1662.  Ignore leading spaces in numbers when casting.
--
test:do_execsql_test(
    "cast-2.1",
    [[
        SELECT CAST('   123' AS integer)
    ]], {
        -- <cast-2.1>
        123
        -- </cast-2.1>
    })

test:do_execsql_test(
    "cast-2.2",
    [[
        SELECT CAST('   -123.456' AS real)
    ]], {
        -- <cast-2.2>
        -123.456
        -- </cast-2.2>
    })

-- ticket #2364.  Use full percision integers if possible when casting
-- to numeric.  Do not fallback to real (and the corresponding 48-bit
-- mantissa) unless absolutely necessary.
--
-- MUST_WORK_TEST waiting for int64 #2363
if 0 > 0 then
test:do_execsql_test(
    "cast-3.1",
    [[
        SELECT CAST(9223372036854774800 AS integer)
    ]], {
        -- <cast-3.1>
        9223372036854774800
        -- </cast-3.1>
    })

test:do_execsql_test(
    "cast-3.2",
    [[
        SELECT CAST(9223372036854774800 AS numeric)
    ]], {
        -- <cast-3.2>
        9223372036854774800
        -- </cast-3.2>
    })

X(239, "X!cmd", [=[["do_realnum_test","cast-3.3","\n  execsql {SELECT CAST(9223372036854774800 AS real)}\n","9.22337203685477e+18"]]=])
test:do_execsql_test(
    "cast-3.4",
    [[
        SELECT CAST(CAST(9223372036854774800 AS real) AS integer)
    ]], {
        -- <cast-3.4>
        9223372036854774784
        -- </cast-3.4>
    })

test:do_execsql_test(
    "cast-3.5",
    [[
        SELECT CAST(-9223372036854774800 AS integer)
    ]], {
        -- <cast-3.5>
        -9223372036854774800
        -- </cast-3.5>
    })

test:do_execsql_test(
    "cast-3.6",
    [[
        SELECT CAST(-9223372036854774800 AS numeric)
    ]], {
        -- <cast-3.6>
        -9223372036854774800
        -- </cast-3.6>
    })

X(251, "X!cmd", [=[["do_realnum_test","cast-3.7","\n  execsql {SELECT CAST(-9223372036854774800 AS real)}\n","-9.22337203685477e+18"]]=])
test:do_execsql_test(
    "cast-3.8",
    [[
        SELECT CAST(CAST(-9223372036854774800 AS real) AS integer)
    ]], {
        -- <cast-3.8>
        -9223372036854774784
        -- </cast-3.8>
    })

test:do_execsql_test(
    "cast-3.11",
    [[
        SELECT CAST('9223372036854774800' AS integer)
    ]], {
        -- <cast-3.11>
        9223372036854774800
        -- </cast-3.11>
    })

test:do_execsql_test(
    "cast-3.12",
    [[
        SELECT CAST('9223372036854774800' AS numeric)
    ]], {
        -- <cast-3.12>
        9223372036854774800
        -- </cast-3.12>
    })

X(263, "X!cmd", [=[["do_realnum_test","cast-3.13","\n  execsql {SELECT CAST('9223372036854774800' AS real)}\n","9.22337203685477e+18"]]=])
test:do_execsql_test(
    "cast-3.14",
    [[
        SELECT CAST(CAST('9223372036854774800' AS real) AS integer)
    ]], {
        -- <cast-3.14>
        9223372036854774784
        -- </cast-3.14>
    })



test:do_execsql_test(
    "cast-3.15",
    [[
        SELECT CAST('-9223372036854774800' AS integer)
    ]], {
        -- <cast-3.15>
        -9223372036854774800
        -- </cast-3.15>
    })

test:do_execsql_test(
    "cast-3.16",
    [[
        SELECT CAST('-9223372036854774800' AS numeric)
    ]], {
        -- <cast-3.16>
        -9223372036854774800
        -- </cast-3.16>
    })

X(277, "X!cmd", [=[["do_realnum_test","cast-3.17","\n  execsql {SELECT CAST('-9223372036854774800' AS real)}\n","-9.22337203685477e+18"]]=])
test:do_execsql_test(
    "cast-3.18",
    [[
        SELECT CAST(CAST('-9223372036854774800' AS real) AS integer)
    ]], {
        -- <cast-3.18>
        -9223372036854774784
        -- </cast-3.18>
    })



if X(286, "X!cmd", "[\"expr\",\"[db eval {PRAGMA encoding}]==\\\"UTF-8\\\"\"]")
 then
    test:do_execsql_test(
        "cast-3.21",
        [[
            SELECT CAST(x'39323233333732303336383534373734383030' AS integer)
        ]], {
            -- <cast-3.21>
            9223372036854774800
            -- </cast-3.21>
        })

    test:do_execsql_test(
        "cast-3.22",
        [[
            SELECT CAST(x'39323233333732303336383534373734383030' AS numeric)
        ]], {
            -- <cast-3.22>
            9223372036854774800
            -- </cast-3.22>
        })

    X(293, "X!cmd", [=[["do_realnum_test","cast-3.23","\n    execsql {SELECT CAST(x'39323233333732303336383534373734383030' AS real)}\n  ","9.22337203685477e+18"]]=])
    test:do_execsql_test(
        "cast-3.24",
        [[
            SELECT CAST(CAST(x'39323233333732303336383534373734383030' AS real)
                        AS integer)
        ]], {
            -- <cast-3.24>
            9223372036854774784
            -- </cast-3.24>
        })



end
end
test:do_execsql_test(
    "case-3.31",
    [[
        SELECT CAST(NULL AS numeric)
    ]], {
        -- <case-3.31>
        ""
        -- </case-3.31>
    })


-- MUST_WORK_TEST prepared statements
-- Test to see if it is possible to trick SQLite into reading past 
-- the end of a blob when converting it to a number.
if 0 > 0 then
test:do_test(
    "cast-3.32.1",
    function()
        blob = 1234567890
        DB = sqlite3_connection_pointer("db")
        STMT = sqlite3_prepare(DB, "SELECT CAST(? AS real)", -1, "TAIL")
        sqlite3_bind_blob("-static", STMT, 1, blob, 5)
        return sqlite3_step(STMT)
    end, {
        -- <cast-3.32.1>
        "SQLITE_ROW"
        -- </cast-3.32.1>
    })

test:do_test(
    "cast-3.32.2",
    function()
        return sqlite3_column_int(STMT, 0)
    end, {
        -- <cast-3.32.2>
        12345
        -- </cast-3.32.2>
    })

test:do_sqlite3_finalize_test(
    "cast-3.32.3",
    STMT, {
        -- <cast-3.32.3>
        "SQLITE_OK"
        -- </cast-3.32.3>
    })
end

test:do_test(
    "cast-4.1",
    function()
        return test:execsql [[
            CREATE TABLE t1(a primary key);
            INSERT INTO t1 VALUES('abc');
            SELECT a, CAST(a AS integer) FROM t1;
        ]]
    end, {
        -- <cast-4.1>
        "abc", 0
        -- </cast-4.1>
    })

test:do_test(
    "cast-4.2",
    function()
        return test:execsql [[
            SELECT CAST(a AS integer), a FROM t1;
        ]]
    end, {
        -- <cast-4.2>
        0, "abc"
        -- </cast-4.2>
    })

test:do_test(
    "cast-4.3",
    function()
        return test:execsql [[
            SELECT a, CAST(a AS integer), a FROM t1;
        ]]
    end, {
        -- <cast-4.3>
        "abc", 0, "abc"
        -- </cast-4.3>
    })

test:do_test(
    "cast-4.4",
    function()
        return test:execsql [[
            SELECT CAST(a AS integer), a, CAST(a AS real), a FROM t1;
        ]]
    end, {
        -- <cast-4.4>
        0, "abc", 0.0, "abc"
        -- </cast-4.4>
    })



test:finish_test()
