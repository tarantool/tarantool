#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(109)

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
-- This file implements regression tests for sql library.  The
-- focus of this file is testing the CAST operator.
--
-- $Id: cast.test,v 1.10 2008/11/06 15:33:04 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Only run these tests if the build includes the CAST operator


-- Tests for the CAST( AS SCALAR), CAST( AS text) and CAST( AS numeric) built-ins
--
test:do_execsql_test(
    "cast-1.1",
    [[
        SELECT x'616263'
    ]], {
        -- <cast-1.1>
        require('varbinary').new("abc")
        -- </cast-1.1>
    })

test:do_execsql_test(
    "cast-1.2",
    [[
        SELECT typeof(x'616263')
    ]], {
        -- <cast-1.2>
        "varbinary"
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
        "string"
        -- </cast-1.4>
    })

test:do_catchsql_test(
    "cast-1.5",
    [[
        SELECT CAST(x'616263' AS NUMBER)
    ]], {
        -- <cast-1.5>
        1, "Type mismatch: can not convert varbinary(x'616263') to number"
        -- </cast-1.5>
    })

test:do_execsql_test(
    "cast-1.7",
    [[
        SELECT CAST(x'616263' AS SCALAR)
    ]], {
        -- <cast-1.7>
        require('varbinary').new("abc")
        -- </cast-1.7>
    })

test:do_execsql_test(
    "cast-1.8",
    [[
        SELECT typeof(CAST(x'616263' AS SCALAR))
    ]], {
        -- <cast-1.8>
        "scalar"
        -- </cast-1.8>
    })

test:do_catchsql_test(
    "cast-1.9",
    [[
        SELECT CAST(x'616263' AS integer)
    ]], {
        -- <cast-1.9>
        1, "Type mismatch: can not convert varbinary(x'616263') to integer"
        -- </cast-1.9>
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
        "NULL"
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
        "NULL"
        -- </cast-1.14>
    })

test:do_execsql_test(
    "cast-1.15",
    [[
        SELECT CAST(NULL AS NUMBER)
    ]], {
        -- <cast-1.15>
        ""
        -- </cast-1.15>
    })

test:do_execsql_test(
    "cast-1.16",
    [[
        SELECT typeof(CAST(NULL AS NUMBER))
    ]], {
        -- <cast-1.16>
        "NULL"
        -- </cast-1.16>
    })

test:do_execsql_test(
    "cast-1.17",
    [[
        SELECT CAST(NULL AS SCALAR)
    ]], {
        -- <cast-1.17>
        ""
        -- </cast-1.17>
    })

test:do_execsql_test(
    "cast-1.18",
    [[
        SELECT typeof(CAST(NULL AS SCALAR))
    ]], {
        -- <cast-1.18>
        "NULL"
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
        "NULL"
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
        "string"
        -- </cast-1.24>
    })

test:do_execsql_test(
    "cast-1.25",
    [[
        SELECT CAST(123 AS NUMBER)
    ]], {
        -- <cast-1.25>
        123
        -- </cast-1.25>
    })

test:do_execsql_test(
    "cast-1.26",
    [[
        SELECT typeof(CAST(123 AS DOUBLE))
    ]], {
        -- <cast-1.26>
        "double"
        -- </cast-1.26>
    })

test:do_execsql_test(
    "cast-1.27",
    [[
        SELECT CAST(123 AS SCALAR)
    ]], {
        -- <cast-1.27>
        123
        -- </cast-1.27>
    })

test:do_execsql_test(
    "cast-1.28",
    [[
        SELECT typeof(CAST(123 AS SCALAR))
    ]], {
        -- <cast-1.28>
        "scalar"
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
        SELECT 123.456e0
    ]], {
        -- <cast-1.31>
        123.456
        -- </cast-1.31>
    })

test:do_execsql_test(
    "cast-1.32",
    [[
        SELECT typeof(123.456e0)
    ]], {
        -- <cast-1.32>
        "double"
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
        SELECT typeof(CAST(123.456e0 AS text))
    ]], {
        -- <cast-1.34>
        "string"
        -- </cast-1.34>
    })

test:do_execsql_test(
    "cast-1.35",
    [[
        SELECT CAST(123.456e0 AS NUMBER)
    ]], {
        -- <cast-1.35>
        123.456
        -- </cast-1.35>
    })

test:do_execsql_test(
    "cast-1.36",
    [[
        SELECT typeof(CAST(123.456e0 AS DOUBLE))
    ]], {
        -- <cast-1.36>
        "double"
        -- </cast-1.36>
    })

test:do_execsql_test(
    "cast-1.37",
    [[
        SELECT CAST(123.456e0 AS SCALAR)
    ]], {
        -- <cast-1.37>
        123.456
        -- </cast-1.37>
    })

test:do_execsql_test(
    "cast-1.38",
    [[
        SELECT typeof(CAST(123.456e0 AS SCALAR))
    ]], {
        -- <cast-1.38>
        "scalar"
        -- </cast-1.38>
    })

test:do_execsql_test(
    "cast-1.39",
    [[
        SELECT CAST(123.456e0 AS integer)
    ]], {
        -- <cast-1.39>
        123
        -- </cast-1.39>
    })

test:do_execsql_test(
    "cast-1.38",
    [[
        SELECT typeof(CAST(123.456e0 AS integer))
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
        "string"
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
        "string"
        -- </cast-1.44>
    })

test:do_catchsql_test(
    "cast-1.45",
    [[
        SELECT CAST('123abc' AS NUMBER)
    ]], {
        -- <cast-1.45>
        1, "Type mismatch: can not convert string('123abc') to number"
        -- </cast-1.45>
    })

test:do_execsql_test(
    "cast-1.48",
    [[
        SELECT typeof(CAST('123abc' AS SCALAR))
    ]], {
        -- <cast-1.48>
        "scalar"
        -- </cast-1.48>
    })

test:do_catchsql_test(
    "cast-1.49",
    [[
        SELECT CAST('123abc' AS integer)
    ]], {
        -- <cast-1.49>
        1, "Type mismatch: can not convert string('123abc') to integer"
        -- </cast-1.49>
    })

test:do_catchsql_test(
    "cast-1.51",
    [[
        SELECT CAST('123.5abc' AS NUMBER)
    ]], {
        -- <cast-1.51>
        1, "Type mismatch: can not convert string('123.5abc') to number"
        -- </cast-1.51>
    })

test:do_catchsql_test(
    "cast-1.53",
    [[
        SELECT CAST('123.5abc' AS integer)
    ]], {
        -- <cast-1.53>
        1, "Type mismatch: can not convert string('123.5abc') to integer"
        -- </cast-1.53>
    })


test:do_execsql_test(
    "case-1.60",
    [[
        SELECT CAST(null AS NUMBER)
    ]], {
        -- <case-1.60>
        ""
        -- </case-1.60>
    })

test:do_execsql_test(
    "case-1.61",
    [[
        SELECT typeof(CAST(null AS NUMBER))
    ]], {
        -- <case-1.61>
        "NULL"
        -- </case-1.61>
    })

test:do_execsql_test(
    "case-1.62",
    [[
        SELECT CAST(1 AS NUMBER)
    ]], {
        -- <case-1.62>
        1.0
        -- </case-1.62>
    })

test:do_execsql_test(
    "case-1.63",
    [[
        SELECT typeof(CAST(1 AS NUMBER))
    ]], {
        -- <case-1.63>
        "number"
        -- </case-1.63>
    })

test:do_execsql_test(
    "case-1.64",
    [[
        SELECT CAST('1' AS NUMBER)
    ]], {
        -- <case-1.64>
        1.0
        -- </case-1.64>
    })

test:do_execsql_test(
    "case-1.65",
    [[
        SELECT typeof(CAST('1' AS NUMBER))
    ]], {
        -- <case-1.65>
        "number"
        -- </case-1.65>
    })

test:do_catchsql_test(
    "case-1.66",
    [[
        SELECT CAST('abc' AS NUMBER)
    ]], {
        -- <case-1.66>
        1, "Type mismatch: can not convert string('abc') to number"
        -- </case-1.66>
    })

test:do_catchsql_test(
    "case-1.68",
    [[
        SELECT CAST(x'31' AS NUMBER)
    ]], {
        -- <case-1.68>
        1, "Type mismatch: can not convert varbinary(x'31') to number"
        -- </case-1.68>
    })

test:do_catchsql_test(
    "case-1.69",
    [[
        SELECT typeof(CAST(x'31' AS NUMBER))
    ]], {
        -- <case-1.69>
        1, "Type mismatch: can not convert varbinary(x'31') to number"
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
        SELECT CAST('   -123.456' AS NUMBER)
    ]], {
        -- <cast-2.2>
        -123.456
        -- </cast-2.2>
    })

-- ticket #2364.  Use full percision integers if possible when casting
-- to numeric.  Do not fallback to real (and the corresponding 48-bit
-- mantissa) unless absolutely necessary.
--

test:do_execsql_test(
    "cast-3.1",
    [[
        SELECT CAST(9223372036854774800 AS integer)
    ]], {
        -- <cast-3.1>
        9223372036854774800LL
        -- </cast-3.1>
    })

test:do_execsql_test(
    "cast-3.2",
    [[
        SELECT CAST(9223372036854774800 AS NUMBER)
    ]], {
        -- <cast-3.2>
        9223372036854774800LL
        -- </cast-3.2>
    })

test:do_execsql_test(
    "cast-3.4",
    [[
        SELECT CAST(CAST(9223372036854774800 AS NUMBER) AS integer)
    ]], {
        -- <cast-3.4>
        9223372036854774800LL
        -- </cast-3.4>
    })

test:do_execsql_test(
    "cast-3.6",
    [[
        SELECT CAST(-9223372036854774800 AS NUMBER)
    ]], {
        -- <cast-3.6>
        -9223372036854774800LL
        -- </cast-3.6>
    })

test:do_execsql_test(
    "cast-3.8",
    [[
        SELECT CAST(CAST(-9223372036854774800 AS NUMBER) AS integer)
    ]], {
        -- <cast-3.8>
        -9223372036854774800LL
        -- </cast-3.8>
    })

test:do_execsql_test(
    "cast-3.11",
    [[
        SELECT CAST('9223372036854774800' AS integer)
    ]], {
        -- <cast-3.11>
        9223372036854774800LL
        -- </cast-3.11>
    })

test:do_execsql_test(
    "cast-3.12",
    [[
        SELECT CAST('9223372036854774800.' AS NUMBER)
    ]], {
        -- <cast-3.12>
        9223372036854774784
        -- </cast-3.12>
    })

test:do_execsql_test(
    "cast-3.14",
    [[
        SELECT CAST(CAST('9223372036854774800.' AS NUMBER) AS integer)
    ]], {
        -- <cast-3.14>
        9223372036854774784LL
        -- </cast-3.14>
    })



test:do_execsql_test(
    "cast-3.15",
    [[
        SELECT CAST('-9223372036854774800' AS integer)
    ]], {
        -- <cast-3.15>
        -9223372036854774800LL
        -- </cast-3.15>
    })

test:do_execsql_test(
    "cast-3.16",
    [[
        SELECT CAST('-9223372036854774800.' AS NUMBER)
    ]], {
        -- <cast-3.16>
        -9223372036854774784
        -- </cast-3.16>
    })

test:do_execsql_test(
    "cast-3.18",
    [[
        SELECT CAST(CAST('-9223372036854774800.' AS NUMBER) AS integer)
    ]], {
        -- <cast-3.18>
        -9223372036854774784LL
        -- </cast-3.18>
    })



if true then --test:execsql("PRAGMA encoding")[1][1]=="UTF-8" then
    test:do_catchsql_test(
        "cast-3.21",
        [[
            SELECT CAST(x'39323233333732303336383534373734383030' AS integer)
        ]], {
            -- <cast-3.21>
            1, "Type mismatch: can not convert "..
               "varbinary(x'39323233333732303336383534373734383030') to integer"
            -- </cast-3.21>
        })

    test:do_catchsql_test(
        "cast-3.22",
        [[
            SELECT CAST(x'393232333337323033363835343737343830302E' AS NUMBER)
        ]], {
            -- <cast-3.22>
            1, "Type mismatch: can not convert "..
               "varbinary(x'393232333337323033363835343737343830302E') "..
               "to number"
            -- </cast-3.22>
        })

    test:do_catchsql_test(
        "cast-3.24",
        [[
            SELECT CAST(CAST(x'39323233333732303336383534373734383030' AS NUMBER)
                        AS integer)
        ]], {
            -- <cast-3.24>
            1, "Type mismatch: can not convert "..
               "varbinary(x'39323233333732303336383534373734383030') to number"
            -- </cast-3.24>
        })
end

test:do_catchsql_test(
    "case-3.25",
    [[
        SELECT CAST(x'31383434363734343037333730393535313631352E' AS NUMBER);
    ]], {
        1, "Type mismatch: can not convert "..
           "varbinary(x'31383434363734343037333730393535313631352E') to number"
    })

test:do_catchsql_test(
    "case-3.26",
    [[
        SELECT CAST(x'3138343436373434303733373039353531363135' AS INT);
    ]], {
        -- <cast-3.21>
        1, "Type mismatch: can not convert "..
           "varbinary(x'3138343436373434303733373039353531363135') to integer"
        -- </cast-3.21>
    })

test:do_execsql_test(
    "case-3.31",
    [[
        SELECT CAST(NULL AS NUMBER)
    ]], {
        -- <case-3.31>
        ""
        -- </case-3.31>
    })


-- MUST_WORK_TEST prepared statements
-- Test to see if it is possible to trick sql into reading past
-- the end of a blob when converting it to a number.
if 0 > 0 then
-- Legacy from the original code. Must be replaced with analogue
-- functions from box.
local sql_prepare = nil
local sql_bind_blob = nil
local sql_step = nil
local sql_column_int = nil
local STMT
test:do_test(
    "cast-3.32.1",
    function()
        local blob = 1234567890
        STMT = sql_prepare("SELECT CAST(? AS NUMBER)", -1, "TAIL")
        sql_bind_blob("-static", STMT, 1, blob, 5)
        return sql_step(STMT)
    end, {
        -- <cast-3.32.1>
        "sql_ROW"
        -- </cast-3.32.1>
    })

test:do_test(
    "cast-3.32.2",
    function()
        return sql_column_int(STMT, 0)
    end, {
        -- <cast-3.32.2>
        12345
        -- </cast-3.32.2>
    })

test:do_sql_finalize_test(
    "cast-3.32.3",
    STMT, {
        -- <cast-3.32.3>
        "sql_OK"
        -- </cast-3.32.3>
    })
end

test:do_test(
    "cast-4.1",
    function()
        return test:catchsql [[
            CREATE TABLE t1(a TEXT primary key);
            INSERT INTO t1 VALUES('abc');
            SELECT a, CAST(a AS integer) FROM t1;
        ]]
    end, {
        -- <cast-4.1>
        1, "Type mismatch: can not convert string('abc') to integer"
        -- </cast-4.1>
    })

test:do_test(
    "cast-4.2",
    function()
        return test:catchsql [[
            SELECT CAST(a AS integer), a FROM t1;
        ]]
    end, {
        -- <cast-4.2>
        1, "Type mismatch: can not convert string('abc') to integer"
        -- </cast-4.2>
    })

test:do_test(
    "cast-4.4",
    function()
        return test:catchsql [[
            SELECT a, CAST(a AS NUMBER), a FROM t1;
        ]]
    end, {
        -- <cast-4.4>
        1, "Type mismatch: can not convert string('abc') to number"
        -- </cast-4.4>
    })

-- gh-4470: Make explicit and implicit casts work according to our rules.

-- Make sure that explicit cast from BOOLEAN to numeric types throws an error.
test:do_catchsql_test(
    "cast-6.1.1",
    [[
        SELECT CAST(TRUE AS UNSIGNED);
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to unsigned"
    })

test:do_catchsql_test(
    "cast-6.1.2",
    [[
        SELECT CAST(FALSE AS UNSIGNED);
    ]], {
        1, "Type mismatch: can not convert boolean(FALSE) to unsigned"
    })

test:do_catchsql_test(
    "cast-6.1.3",
    [[
        SELECT CAST(TRUE AS INTEGER);
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to integer"
    })

test:do_catchsql_test(
    "cast-6.1.4",
    [[
        SELECT CAST(FALSE AS INTEGER);
    ]], {
        1, "Type mismatch: can not convert boolean(FALSE) to integer"
    })

test:do_catchsql_test(
    "cast-6.1.5",
    [[
        SELECT CAST(TRUE AS DOUBLE);
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to double"
    })

test:do_catchsql_test(
    "cast-6.1.6",
    [[
        SELECT CAST(FALSE AS DOUBLE);
    ]], {
        1, "Type mismatch: can not convert boolean(FALSE) to double"
    })

test:do_catchsql_test(
    "cast-6.1.7",
    [[
        SELECT CAST(TRUE AS NUMBER);
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to number"
    })

test:do_catchsql_test(
    "cast-6.1.8",
    [[
        SELECT CAST(FALSE AS NUMBER);
    ]], {
        1, "Type mismatch: can not convert boolean(FALSE) to number"
    })

-- Make sure that explicit cast numeric value to BOOLEAN throws an error.
test:do_catchsql_test(
    "cast-6.2.1",
    [[
        SELECT CAST(0 AS BOOLEAN);
    ]], {
        1, "Type mismatch: can not convert integer(0) to boolean"
    })

test:do_catchsql_test(
    "cast-6.2.2",
    [[
        SELECT CAST(-1 AS BOOLEAN);
    ]], {
        1, "Type mismatch: can not convert integer(-1) to boolean"
    })

test:do_catchsql_test(
    "cast-6.2.3",
    [[
        SELECT CAST(1.5e0 AS BOOLEAN);
    ]], {
        1, "Type mismatch: can not convert double(1.5) to boolean"
    })

test:do_catchsql_test(
    "cast-6.2.4",
    [[
        SELECT CAST(CAST(1 AS NUMBER) AS BOOLEAN);
    ]], {
        1, "Type mismatch: can not convert number(1) to boolean"
    })

-- Make sure that explicit cast from VARBINARY to numeric types throws an error.
test:do_catchsql_test(
    "cast-7.1.1",
    [[
        SELECT CAST(x'31' AS UNSIGNED);
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to unsigned"
    })

test:do_catchsql_test(
    "cast-7.1.2",
    [[
        SELECT CAST(x'31' AS INTEGER);
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to integer"
    })

test:do_catchsql_test(
    "cast-7.1.3",
    [[
        SELECT CAST(x'31' AS DOUBLE);
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to double"
    })

test:do_catchsql_test(
    "cast-7.1.4",
    [[
        SELECT CAST(x'31' AS NUMBER);
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to number"
    })

-- Make sure that not NULL-terminated can be cast to BOOLEAN.
test:do_execsql_test(
    "cast-8",
    [[
        SELECT CAST(substr('true       ', 0, 6) AS BOOLEAN);
    ]], {
        true
    })

-- Make sure that implicit conversion of numeric values is precise.
test:execsql([[
    CREATE TABLE t2 (i INTEGER PRIMARY KEY AUTOINCREMENT, a INTEGER, b DOUBLE);
    CREATE TABLE t3 (i INTEGER PRIMARY KEY AUTOINCREMENT, s STRING);
    CREATE TABLE t4 (i INTEGER PRIMARY KEY AUTOINCREMENT, v VARBINARY);
    CREATE TABLE t5 (i INTEGER PRIMARY KEY AUTOINCREMENT, u UUID);
]])

test:do_execsql_test(
    "cast-9.1.1",
    [[
        INSERT INTO t2(a) VALUES(1.0e0);
        SELECT a FROM t2 WHERE i = 1;
    ]], {
        1
    })

test:do_catchsql_test(
    "cast-9.1.2",
    [[
        INSERT INTO t2(a) VALUES(1.5e0);
    ]], {
        1, "Type mismatch: can not convert double(1.5) to integer"
    })

test:do_execsql_test(
    "cast-9.1.3",
    [[
        INSERT INTO t2(b) VALUES(10000000000000000);
        SELECT b FROM t2 WHERE i = 2;
    ]], {
        10000000000000000
    })

test:do_catchsql_test(
    "cast-9.1.4",
    [[
        INSERT INTO t2(b) VALUES(10000000000000001);
    ]], {
        1, "Type mismatch: can not convert integer(10000000000000001) to double"
    })

-- Make sure that UUID cannot be implicitly cast to STRING.
local uuid = "CAST('11111111-1111-1111-1111-111111111111' AS UUID)";
test:do_catchsql_test(
    "cast-9.2",
    [[
        INSERT INTO t3(s) VALUES(]]..uuid..[[);
    ]], {
        1, "Type mismatch: can not convert "..
           "uuid(11111111-1111-1111-1111-111111111111) to string"
    })

-- Make sure that UUID cannot be implicitly cast to VARBINARY.
test:do_catchsql_test(
    "cast-9.3",
    [[
        INSERT INTO t4(v) VALUES(]]..uuid..[[);
    ]], {
        1, "Type mismatch: can not convert "..
           "uuid(11111111-1111-1111-1111-111111111111) to varbinary"
    })

-- Make sure that STRING and VARBINARY cannot be implicitly cast to UUID.
test:do_catchsql_test(
    "cast-9.4.1",
    [[
        INSERT INTO t5(u) VALUES('11111111-1111-1111-1111-111111111111');
    ]], {
        1, "Type mismatch: can not convert "..
           "string('11111111-1111-1111-1111-111111111111') to uuid"
    })

test:do_catchsql_test(
    "cast-9.4.2",
    [[
        INSERT INTO t5(u) VALUES(x'11111111111111111111111111111111');
    ]], {
        1, "Type mismatch: can not convert "..
           "varbinary(x'11111111111111111111111111111111') to uuid"
    })

test:execsql([[
    DROP TABLE t1;
    DROP TABLE t2;
    DROP TABLE t3;
    DROP TABLE t4;
    DROP TABLE t5;
]])

--
-- Make sure that implicit cast from STRING to number was removed during
-- comparison where index is not used.
--

test:do_catchsql_test(
    "cast-10.1",
    [[
        SELECT 1 < '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to number"
    })

test:do_catchsql_test(
    "cast-10.2",
    [[
        SELECT '1' < 2;
    ]], {
        1, "Type mismatch: can not convert integer(2) to string"
    })

test:do_catchsql_test(
    "cast-10.3",
    [[
        SELECT 1.5 < '2';
    ]], {
        1, "Type mismatch: can not convert string('2') to number"
    })

test:do_catchsql_test(
    "cast-10.4",
    [[
        SELECT '1' < 2.5e0;
    ]], {
        1, "Type mismatch: can not convert double(2.5) to string"
    })

-- Make sure that search using index in field type number work right.
test:do_execsql_test(
    "cast-11",
    [[
        CREATE TABLE t6(d DOUBLE PRIMARY KEY);
        INSERT INTO t6 VALUES(10000000000000000);
        SELECT d FROM t6 WHERE d < 10000000000000001 and d > 9999999999999999;
        DROP TABLE t6;
    ]], {
        10000000000000000
    })

-- Make sure that there is no unnecessary implicit casts in IN operator.
test:do_execsql_test(
    "cast-12",
    [[
        SELECT 1 IN (SELECT '1');
    ]], {
        false
    })

test:finish_test()
